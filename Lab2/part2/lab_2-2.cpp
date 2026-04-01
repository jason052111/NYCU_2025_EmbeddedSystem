#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <linux/fb.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/stat.h>   // for mkdir
#include <sys/types.h>
#include <sstream>
#include <string>
#include <termios.h>
#include <stdio.h>
#include <sys/select.h>

// framebuffer info
struct framebuffer_info
{
    uint32_t bits_per_pixel;
    uint32_t xres_virtual;
};

struct framebuffer_info get_framebuffer_info(const char *framebuffer_device_path);

// ----------- 非阻塞讀鍵盤 -----------
int getch_noblock() {
    struct termios oldt, newt;
    int ch;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);   // 關閉行緩衝 & echo
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    struct timeval tv = {0L, 0L};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);

    int ret = select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);

    if (ret > 0) {
        ch = getchar();
    } else {
        ch = -1;  // 沒有輸入
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);  // 還原 terminal 狀態
    return ch;
}
// ---------------------------------

int main(int argc, const char *argv[])
{
    cv::Mat frame;
    cv::Size frame_size;

    cv::VideoCapture camera(2);
    if (!camera.isOpened())
    {
        std::cerr << "Could not open video device." << std::endl;
        return 1;
    }

    // 設定攝影機解析度
    camera.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    camera.set(cv::CAP_PROP_FRAME_HEIGHT, 480);

    // 取得 framebuffer 資訊
    framebuffer_info fb_info = get_framebuffer_info("/dev/fb0");
    std::ofstream ofs("/dev/fb0", std::ios::out | std::ios::binary);
    if (!ofs.is_open())
    {
        std::cerr << "Error: cannot open /dev/fb0" << std::endl;
        return -1;
    }

    int screen_width  = fb_info.xres_virtual;
    int screen_bpp    = fb_info.bits_per_pixel / 8;
    int screen_height = 480; 

    // -------- 每次 run 建立新 screenshot 資料夾 --------
    int run_id = 0;
    std::string base_path = "/mnt/sdcard";
    std::string screenshot_dir;

    while (true) {
        std::ostringstream dirname;
        dirname << base_path << "/screenshot_" << run_id;
        screenshot_dir = dirname.str();

        struct stat st = {0};
        if (stat(screenshot_dir.c_str(), &st) == -1) {
            // 不存在 → 建立並使用這個資料夾
            mkdir(screenshot_dir.c_str(), 0777);
            break;
        }
        run_id++;
    }

    std::cout << "Saving screenshots into: " << screenshot_dir << std::endl;

    int screenshot_id = -1;

    while (true)
    {
        camera >> frame;
        if (frame.empty()) break;

        frame_size = frame.size();

        // -------- 保持比例縮放 --------
        int target_w = screen_width;
        int target_h = screen_height;

        double aspect_input  = (double)frame_size.width / frame_size.height;
        double aspect_output = (double)target_w / target_h;

        cv::Mat resized;
        if (aspect_input > aspect_output)
        {
            int new_w = target_w;
            int new_h = (int)(target_w / aspect_input);
            cv::resize(frame, resized, cv::Size(new_w, new_h));

            int top = (target_h - new_h) / 2;
            int bottom = target_h - new_h - top;
            cv::copyMakeBorder(resized, resized, top, bottom, 0, 0,
                               cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
        }
        else
        {
            int new_h = target_h;
            int new_w = (int)(target_h * aspect_input);
            cv::resize(frame, resized, cv::Size(new_w, new_h));

            int left = (target_w - new_w) / 2;
            int right = target_w - new_w - left;
            cv::copyMakeBorder(resized, resized, 0, 0, left, right,
                               cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
        }

        // -------- 顏色轉換 --------
        cv::Mat frame_BGR565;
        cv::cvtColor(resized, frame_BGR565, cv::COLOR_BGR2BGR565);

        // -------- 寫到 framebuffer --------
        for (int y = 0; y < target_h; y++)
        {
            ofs.seekp(y * fb_info.xres_virtual * screen_bpp);
            ofs.write(reinterpret_cast<const char*>(frame_BGR565.ptr(y)),
                      target_w * screen_bpp);
        }

        // -------- 讀鍵盤輸入 --------
        int key = getch_noblock();
        if (key == 'q') break;

        if (key == 'c')
        {
            screenshot_id++;
            std::ostringstream filename;
            filename << screenshot_dir << "/shot_" << screenshot_id << ".bmp";

            cv::imwrite(filename.str(), frame); // 存 BGR 原圖
            std::cout << "Screenshot saved: " << filename.str() << std::endl;
        }
    }

    camera.release();
    ofs.close();
    return 0;
}

struct framebuffer_info get_framebuffer_info(const char *framebuffer_device_path)
{
    struct framebuffer_info fb_info;
    struct fb_var_screeninfo screen_info;

    int fd = open(framebuffer_device_path, O_RDWR);
    if (fd == -1)
    {
        perror("Error opening framebuffer device");
        exit(1);
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &screen_info))
    {
        perror("Error reading screen info");
        close(fd);
        exit(2);
    }

    fb_info.xres_virtual   = screen_info.xres_virtual;
    fb_info.bits_per_pixel = screen_info.bits_per_pixel;

    close(fd);
    return fb_info;
}
