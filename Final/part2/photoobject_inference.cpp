#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <termios.h>
#include <opencv2/opencv.hpp>
#include <map>
#include <string>
#include "ncnn/net.h" 

// ==================== 1. 設定區 ====================

// 閾值設定
static const float PROB_THRESHOLD = 0.215f;
static const float NMS_THRESHOLD  = 0.45f;

// --- 第一組：COCO 21類 (對應 yolov8x) ---
static const int SIZE_COCO = 640; // v8x 用 640
static std::map<int, std::string> CLASSES_COCO = {
    {4, "Airplane"}, {66, "Keyboard"}, {72, "Refrigerator"}, {32, "Baseball"},
    {41, "Mug"}, {76, "Scissors"}, {65, "Controller"}, {22, "Zebra"},
    {15, "Cat"}, {3, "Motorcycle"}, {14, "Pigeon"}, {47, "Apple"},
    {53, "Pizza"}, {25, "Umbrella"}, {39, "Bottle"}, {40, "Glass"},
    {49, "Orange"}, {67, "Phone"}, {42, "Fork"}, {64, "Mouse"}, {43, "Knife"},
    {73, "Sticky Note"}, {44, "Dart"}, {74, "Controller"}  // ★ 新增這行：把 Book (73) 顯示成 Sticky Note
};
// --- 第二組：你的 Custom Model (對應 best.pt) ---
// 根據你的要求：拿掉 Pencil (ID 2)
static const int SIZE_CUSTOM = 960; // v8s custom 用 960
static std::map<int, std::string> CLASSES_CUSTOM = {
    {0, "Tissue"},
    {1, "Dart"},
    {2, "Pencil"}, // <--- 這裡註解掉，程式就會自動忽略它
    {3, "Poker card"}
};

// ==================== 2. 結構與工具函數 ====================

struct Object {
    cv::Rect_<float> rect;
    int label;
    float prob;
};

// 非阻塞鍵盤讀取
static int getch_noblock() {
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    struct timeval tv = {0L, 0L};
    fd_set fds; FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
    int ch = (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) ? getchar() : -1;
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}

// Framebuffer 資訊
struct framebuffer_info {
    uint32_t bits_per_pixel;
    uint32_t xres;
    uint32_t yres;
    uint32_t line_length;
    uint32_t smem_len;
};

static framebuffer_info get_framebuffer_info(const char *framebuffer_device_path) {
    framebuffer_info fb{};
    fb_var_screeninfo vinfo{};
    fb_fix_screeninfo finfo{};
    int fd = open(framebuffer_device_path, O_RDWR);
    if (fd == -1) return fb;
    ioctl(fd, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fd, FBIOGET_FSCREENINFO, &finfo);
    fb.bits_per_pixel = vinfo.bits_per_pixel;
    fb.xres = vinfo.xres;
    fb.yres = vinfo.yres;
    fb.line_length = finfo.line_length;
    fb.smem_len = finfo.smem_len;
    close(fd);
    return fb;
}

// NMS
static void nms_sorted_bboxes(const std::vector<Object>& faceobjects, std::vector<Object>& picked, float nms_threshold) {
    picked.clear();
    const int n = faceobjects.size();
    std::vector<float> areas(n);
    for (int i = 0; i < n; i++) areas[i] = faceobjects[i].rect.area();

    for (int i = 0; i < n; i++) {
        const Object& a = faceobjects[i];
        int keep = 1;
        for (int j = 0; j < (int)picked.size(); j++) {
            const Object& b = picked[j];
            float inter_area = (a.rect & b.rect).area();
            if (inter_area / (areas[i] + b.rect.area() - inter_area) > nms_threshold)
                keep = 0;
        }
        if (keep) picked.push_back(a);
    }
}

// 通用解碼函式 (傳入 target_classes 來決定要抓哪些)
static void decode_yolov8(const ncnn::Mat& out, std::vector<Object>& objects, float scale, int wpad, int hpad, const std::map<int, std::string>& target_classes) {
    objects.clear();
    const int num_anchors = out.w;
    const int num_channels = out.h; 

    for (int i = 0; i < num_anchors; i++) {
        float max_score = 0.f;
        int max_class_id = -1;

        // 從 index 4 開始找最大機率
        for (int k = 4; k < num_channels; k++) {
            float score = out.row(k)[i];
            if (score > max_score) {
                max_score = score;
                max_class_id = k - 4;
            }
        }

        // ★關鍵：只抓 map 裡面有的類別★
        if (max_score >= PROB_THRESHOLD && target_classes.count(max_class_id)) {
            float cx = out.row(0)[i];
            float cy = out.row(1)[i];
            float w  = out.row(2)[i];
            float h  = out.row(3)[i];

            float x0 = (cx - w * 0.5f - (wpad / 2)) / scale;
            float y0 = (cy - h * 0.5f - (hpad / 2)) / scale;
            float width = w / scale;
            float height = h / scale;

            Object obj;
            obj.rect.x = x0;
            obj.rect.y = y0;
            obj.rect.width = width;
            obj.rect.height = height;
            obj.label = max_class_id;
            obj.prob = max_score;
            objects.push_back(obj);
        }
    }
}

// 封裝好的推論與繪圖函式 (傳入不同的模型與參數)
void run_inference_and_draw(ncnn::Net& net, cv::Mat& img, int target_size, const std::map<int, std::string>& target_classes, const char* model_name) {
    printf("Running %s (Input: %d)...\n", model_name, target_size);

    int img_w = img.cols;
    int img_h = img.rows;
    int w = img_w;
    int h = img_h;
    float scale = 1.f;
    if (w > h) {
        scale = (float)target_size / w;
        w = target_size;
        h = h * scale;
    } else {
        scale = (float)target_size / h;
        h = target_size;
        w = w * scale;
    }
    
    ncnn::Mat in = ncnn::Mat::from_pixels_resize(img.data, ncnn::Mat::PIXEL_BGR2RGB, img_w, img_h, w, h);
    int wpad = target_size - w;
    int hpad = target_size - h;
    ncnn::Mat in_pad;
    // 填充 0 或 114
    ncnn::copy_make_border(in, in_pad, hpad/2, hpad - hpad/2, wpad/2, wpad - wpad/2, ncnn::BORDER_CONSTANT, 114.f);
    
    const float norm_vals[3] = {1/255.f, 1/255.f, 1/255.f};
    in_pad.substract_mean_normalize(0, norm_vals);

    ncnn::Extractor ex = net.create_extractor();
    ex.input("images", in_pad);
    ncnn::Mat out;
    ex.extract("output0", out);

    std::vector<Object> proposals;
    decode_yolov8(out, proposals, scale, wpad, hpad, target_classes);
    
    std::vector<Object> objects;
    nms_sorted_bboxes(proposals, objects, NMS_THRESHOLD);

    // 畫圖
    for (const auto& obj : objects) {
        int x0 = std::max(0, std::min((int)obj.rect.x, img.cols - 1));
        int y0 = std::max(0, std::min((int)obj.rect.y, img.rows - 1));
        int x1 = std::max(0, std::min((int)(obj.rect.x + obj.rect.width), img.cols - 1));
        int y1 = std::max(0, std::min((int)(obj.rect.y + obj.rect.height), img.rows - 1));

        // 畫框
        cv::rectangle(img, cv::Point(x0, y0), cv::Point(x1, y1), cv::Scalar(0, 255, 0), 2);

        // 找名字 (從傳入的 map 找)
        // 使用 .at() 因為我們確定 key 存在 (在 decode 時檢查過)
        std::string label_text = target_classes.at(obj.label);
        std::string text = label_text + " " + std::to_string((int)(obj.prob * 100)) + "%";
        
        int baseLine = 0;
        cv::Size label_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.6, 1, &baseLine);
        cv::rectangle(img, cv::Rect(cv::Point(x0, y0 - label_size.height),
                                  cv::Size(label_size.width, label_size.height + baseLine)),
                      cv::Scalar(0, 255, 0), -1);
        
        cv::putText(img, text, cv::Point(x0, y0), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 0), 1);
        // printf("  Found: %s\n", text.c_str());
    }
}

// ==================== 3. Main ====================
int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " [image_path]" << std::endl;
        return -1;
    }

    const char* imagepath = argv[1];

    // --- 1. 初始化 NCNN 網路 (兩個) ---
    ncnn::Net net_coco;
    ncnn::Net net_custom;
    
    net_coco.opt.use_vulkan_compute = false;
    net_coco.opt.num_threads = 4;
    net_custom.opt.use_vulkan_compute = false;
    net_custom.opt.num_threads = 4;

    // 載入 yolov8x (COCO)
    if (net_coco.load_param("yolov8x.param") == -1 || net_coco.load_model("yolov8x.bin") == -1) {
        std::cerr << "Error: Failed to load yolov8x.param/bin" << std::endl;
        return -1;
    }

    // 載入 best (Custom)
    if (net_custom.load_param("best.param") == -1 || net_custom.load_model("best.bin") == -1) {
        std::cerr << "Error: Failed to load best.param/bin" << std::endl;
        return -1;
    }

    // --- 2. 讀取圖片 ---
    cv::Mat img = cv::imread(imagepath);
    if (img.empty()) {
        std::cerr << "Error: Check image path." << std::endl;
        return -1;
    }

    // --- 3. 執行兩次推論 (結果會疊加在 img 上) ---
    
    // 第一跑：COCO (v8x, 640)
    run_inference_and_draw(net_coco, img, SIZE_COCO, CLASSES_COCO, "YOLOv8x (COCO)");

    // 第二跑：Custom (best, 960)
    // 這裡會忽略 Pencil，因為 CLASSES_CUSTOM 裡沒有 ID 2
    run_inference_and_draw(net_custom, img, SIZE_CUSTOM, CLASSES_CUSTOM, "Custom Best (v8s)");


    // --- 4. 存檔 ---
    std::string input_path = imagepath; 
    std::string output_filename;
    size_t last_dot = input_path.find_last_of(".");
    if (last_dot == std::string::npos) output_filename = input_path + "_result.jpg";
    else output_filename = input_path.substr(0, last_dot) + "_result.jpg";

    cv::imwrite(output_filename, img);
    // std::cout << "All results saved to: " << output_filename << std::endl;


    // --- 5. 顯示到 Framebuffer (不用改) ---
    int fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd == -1) {
        std::cout << "Cannot open fb0, skipping display." << std::endl;
        return 0;
    }
    framebuffer_info vinfo_s = get_framebuffer_info("/dev/fb0"); // Reuse helper logic
    // ... 為了簡化，直接用手動取一次 fb info
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo);
    long screensize = finfo.smem_len;
    uint8_t* fbp = (uint8_t*)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);

    // 建立黑色畫布，將圖片置中
    cv::Mat canvas(vinfo.yres, vinfo.xres, CV_8UC3, cv::Scalar(0, 0, 0));
    float screen_scale = std::min((float)vinfo.xres / img.cols, (float)vinfo.yres / img.rows);
    cv::Mat img_resized;
    cv::resize(img, img_resized, cv::Size(), screen_scale, screen_scale);
    int startX = (vinfo.xres - img_resized.cols) / 2;
    int startY = (vinfo.yres - img_resized.rows) / 2;
    img_resized.copyTo(canvas(cv::Rect(startX, startY, img_resized.cols, img_resized.rows)));

    cv::Mat frame_out;
    if (vinfo.bits_per_pixel == 16) {
         cv::cvtColor(canvas, frame_out, cv::COLOR_BGR2BGR565);
         for (int y = 0; y < vinfo.yres; y++) {
             long location = (y + vinfo.yoffset) * finfo.line_length;
             memcpy(fbp + location, frame_out.ptr(y), vinfo.xres * 2);
         }
    } else if (vinfo.bits_per_pixel == 32) {
         cv::cvtColor(canvas, frame_out, cv::COLOR_BGR2BGRA);
         for (int y = 0; y < vinfo.yres; y++) {
             long location = (y + vinfo.yoffset) * finfo.line_length;
             memcpy(fbp + location, frame_out.ptr(y), vinfo.xres * 4);
         }
    }

    // std::cout << "Displayed. Press 'q' to quit." << std::endl;
    while(true) {
        int key = getch_noblock();
        if (key == 'q' || key == 'Q') break;
        usleep(50000);
    }

    munmap(fbp, screensize);
    close(fbfd);
    return 0;
}