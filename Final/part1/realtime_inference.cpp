#include <fcntl.h>
#include <iostream>
#include <linux/fb.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sstream>
#include <string>
#include <termios.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <cstring>
#include <stdint.h>
#include <vector>
#include <algorithm>
#include <map>
#include <chrono> // 新增計時用
#include "ncnn/net.h" 

// ============ 1. 設定區 ============

static const int TARGET_SIZE = 320; 
static const float PROB_THRESHOLD = 0.1f; // 稍微提高一點減少誤判
static const float NMS_THRESHOLD  = 0.45f;

// 是否要在 Framebuffer 上全螢幕縮放 (true = 慢但滿版, false = 快但小圖)
static const bool FULLSCREEN_DISPLAY = true; 

static std::map<int, std::string> TARGET_CLASSES = {
    {44, "spoon"}, {46, "banana"}, {66, "keyboard"}, {67, "cell phone"},
    {73, "book"}, {76, "Scissors"}, {39, "bottle"}, {41, "cup"}
};

struct Object {
    cv::Rect_<float> rect;
    int label;
    float prob;
};

// ============ 2. Framebuffer 工具 (不變) ============
struct framebuffer_info {
    uint32_t bits_per_pixel; uint32_t xres; uint32_t yres; uint32_t line_length; uint32_t smem_len;
};

static framebuffer_info get_framebuffer_info(const char *framebuffer_device_path) {
    framebuffer_info fb{}; fb_var_screeninfo vinfo{}; fb_fix_screeninfo finfo{};
    int fd = open(framebuffer_device_path, O_RDWR);
    if (fd == -1) return fb;
    ioctl(fd, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fd, FBIOGET_FSCREENINFO, &finfo);
    fb.bits_per_pixel = vinfo.bits_per_pixel; fb.xres = vinfo.xres; fb.yres = vinfo.yres;
    fb.line_length = finfo.line_length; fb.smem_len = finfo.smem_len;
    close(fd); return fb;
}

// ============ 3. 非阻塞鍵盤讀取 (不變) ============
static int getch_noblock() {
    struct termios oldt, newt; int ch; tcgetattr(STDIN_FILENO, &oldt); newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO); tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    struct timeval tv = {0L, 0L}; fd_set fds; FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
    int ret = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    if (ret > 0) ch = getchar(); else ch = -1;
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt); return ch;
}

// ============ 4. YOLOv8 Post-Processing (不變) ============
static void nms_sorted_bboxes(const std::vector<Object>& faceobjects, std::vector<Object>& picked, float nms_threshold) {
    picked.clear(); const int n = faceobjects.size(); std::vector<float> areas(n);
    for (int i = 0; i < n; i++) areas[i] = faceobjects[i].rect.area();
    for (int i = 0; i < n; i++) {
        const Object& a = faceobjects[i]; int keep = 1;
        for (int j = 0; j < (int)picked.size(); j++) {
            const Object& b = picked[j];
            float inter_area = (a.rect & b.rect).area();
            if (inter_area / (areas[i] + b.rect.area() - inter_area) > nms_threshold) keep = 0;
        }
        if (keep) picked.push_back(a);
    }
}

static void decode_yolov8(const ncnn::Mat& out, std::vector<Object>& objects, float scale, int wpad, int hpad) {
    objects.clear();
    const int num_anchors = out.w; const int num_channels = out.h; 
    // 優化: 預先計算 TARGET_CLASSES 的 key，減少 map 查詢次數 (略，量不大)
    for (int i = 0; i < num_anchors; i++) {
        float max_score = 0.f; int max_class_id = -1;
        for (int k = 4; k < num_channels; k++) {
            float score = out.row(k)[i];
            if (score > max_score) { max_score = score; max_class_id = k - 4; }
        }
        if (max_score >= PROB_THRESHOLD && TARGET_CLASSES.count(max_class_id)) {
            float cx = out.row(0)[i], cy = out.row(1)[i], w = out.row(2)[i], h = out.row(3)[i];
            float x0 = (cx - w * 0.5f - (wpad / 2)) / scale;
            float y0 = (cy - h * 0.5f - (hpad / 2)) / scale;
            Object obj; obj.rect = cv::Rect_<float>(x0, y0, w/scale, h/scale);
            obj.label = max_class_id; obj.prob = max_score;
            objects.push_back(obj);
        }
    }
}

static void draw_objects(cv::Mat& image, const std::vector<Object>& objects, float fps) {
    for (const auto& obj : objects) {
        int x = std::max(0, std::min((int)obj.rect.x, image.cols - 1));
        int y = std::max(0, std::min((int)obj.rect.y, image.rows - 1));
        int w = std::max(0, std::min((int)obj.rect.width, image.cols - x));
        int h = std::max(0, std::min((int)obj.rect.height, image.rows - y));
        cv::rectangle(image, cv::Rect(x, y, w, h), cv::Scalar(0, 255, 0), 2);
        std::string text = TARGET_CLASSES[obj.label] + " " + std::to_string((int)(obj.prob * 100)) + "%";
        cv::putText(image, text, cv::Point(x, y - 5), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
    }
    // 顯示 FPS
    std::string fps_text = "FPS: " + std::to_string((int)fps);
    cv::putText(image, fps_text, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2);
}

// ============ 5. Main ============

int main(int argc, char** argv) {
    // --- Framebuffer ---
    framebuffer_info fb = get_framebuffer_info("/dev/fb0");
    int fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd < 0) return -1;
    uint8_t* fbmem = (uint8_t*)mmap(nullptr, fb.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);

    // --- Camera ---
    cv::VideoCapture camera(2);
    // 設定 640x480 (太高會拖慢讀取和後處理)
    camera.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    camera.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    if (!camera.isOpened()) return -1;

    // --- NCNN ---
    ncnn::Net yolov8;
    yolov8.opt.use_vulkan_compute = false; 
    yolov8.opt.num_threads = 4; // Pi 4/5 請設為 4

    // ★★★ 請確保載入的是優化過的 FP16 模型 (best_opt) ★★★
    if (yolov8.load_param("best_opt.param") == -1 || yolov8.load_model("best_opt.bin") == -1) {
        std::cerr << "Cannot load best_opt model! Trying standard..." << std::endl;
        yolov8.load_param("yolov8n.param");
        yolov8.load_model("yolov8n.bin");
    }

    cv::Mat frame;
    cv::Mat canvas(fb.yres, fb.xres, CV_8UC3, cv::Scalar(0,0,0)); 
    std::cout << "Running... Press 'q' to quit." << std::endl;

    auto last_time = std::chrono::steady_clock::now();
    float fps = 0;

    while (true) {
        // FPS Calculation
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
        if (dt > 0) fps = 1000.0f / dt;
        last_time = now;

        camera >> frame;
        if (frame.empty()) continue;

        // --- NCNN Preprocessing ---
        int img_w = frame.cols;
        int img_h = frame.rows;
        int w = img_w, h = img_h;
        float scale = 1.f;
        if (w > h) { scale = (float)TARGET_SIZE / w; w = TARGET_SIZE; h = h * scale; }
        else       { scale = (float)TARGET_SIZE / h; h = TARGET_SIZE; w = w * scale; }

        ncnn::Mat in = ncnn::Mat::from_pixels_resize(frame.data, ncnn::Mat::PIXEL_BGR2RGB, img_w, img_h, w, h);
        int wpad = TARGET_SIZE - w; int hpad = TARGET_SIZE - h;
        ncnn::Mat in_pad;
        ncnn::copy_make_border(in, in_pad, hpad/2, hpad-hpad/2, wpad/2, wpad-wpad/2, ncnn::BORDER_CONSTANT, 114.f);
        
        const float norm_vals[3] = {1/255.f, 1/255.f, 1/255.f};
        in_pad.substract_mean_normalize(0, norm_vals);

        // --- Inference ---
        ncnn::Extractor ex = yolov8.create_extractor();
        ex.input("images", in_pad);
        ncnn::Mat out;
        ex.extract("output0", out);

        // --- Post-processing ---
        std::vector<Object> proposals, objects;
        decode_yolov8(out, proposals, scale, wpad, hpad);
        nms_sorted_bboxes(proposals, objects, NMS_THRESHOLD);
        draw_objects(frame, objects, fps);

        // --- Display Optimization (關鍵修改) ---
        // 清空 Canvas 的方法優化：不需要整張清空，只要確保新圖覆蓋即可
        // canvas.setTo(cv::Scalar(0,0,0)); // 這一行其實很慢，如果滿版顯示可以拿掉

        int new_w = fb.xres, new_h = fb.yres;
        int off_x = 0, off_y = 0;

        if (FULLSCREEN_DISPLAY) {
            // 計算縮放尺寸
            double in_aspect = (double)frame.cols / frame.rows;
            double out_aspect = (double)fb.xres / fb.yres;
            if (in_aspect > out_aspect) {
                new_w = fb.xres; new_h = (int)(fb.xres / in_aspect); off_y = (fb.yres - new_h) / 2;
            } else {
                new_h = fb.yres; new_w = (int)(fb.yres * in_aspect); off_x = (fb.xres - new_w) / 2;
            }
            // ★ 使用 INTER_NEAREST 加速縮放 (比 LINEAR 快很多) ★
            cv::Mat resized_frame;
            cv::resize(frame, resized_frame, cv::Size(new_w, new_h), 0, 0, cv::INTER_NEAREST);
            
            // 複製到 Canvas
            resized_frame.copyTo(canvas(cv::Rect(off_x, off_y, new_w, new_h)));
        } else {
            // 不縮放模式 (最快)，直接把 640x480 貼到中間
            new_w = frame.cols; new_h = frame.rows;
            off_x = (fb.xres - new_w) / 2;
            off_y = (fb.yres - new_h) / 2;
            
            // 如果超出範圍要裁切 (略)，假設螢幕大於 640x480
            frame.copyTo(canvas(cv::Rect(off_x, off_y, new_w, new_h)));
        }

        // 寫入 Framebuffer
        if (fb.bits_per_pixel == 16) {
            cv::Mat frame565;
            cv::cvtColor(canvas, frame565, cv::COLOR_BGR2BGR565);
            // 記憶體複製
            for (int y = 0; y < fb.yres; y++) {
                std::memcpy(fbmem + y * fb.line_length, frame565.ptr(y), fb.xres * 2);
            }
        } else if (fb.bits_per_pixel == 32) {
             cv::Mat frame32;
             cv::cvtColor(canvas, frame32, cv::COLOR_BGR2BGRA);
             for (int y = 0; y < fb.yres; y++) {
                std::memcpy(fbmem + y * fb.line_length, frame32.ptr(y), fb.xres * 4);
             }
        }

        if (getch_noblock() == 'q') break;
    }

    munmap(fbmem, fb.smem_len);
    close(fbfd);
    return 0;
}