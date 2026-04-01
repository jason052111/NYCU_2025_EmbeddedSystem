#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <linux/fb.h>
#include <opencv2/imgproc/imgproc.hpp>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>

// ======== 加入 stb_image.h ========
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// ===== Framebuffer Info =====
struct framebuffer_info {
    uint32_t bits_per_pixel;
    uint32_t xres_virtual;
};
struct framebuffer_info get_framebuffer_info(const char *framebuffer_device_path);

// ===== 非阻塞讀鍵盤 =====
int getch_noblock() {
    struct termios oldt, newt;
    int ch;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO); // 關閉行緩衝與回顯
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    struct timeval tv = {0L, 0L};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);

    int ret = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    ch = (ret > 0) ? getchar() : -1;

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}

// ===== 主程式 =====
int main()
{
    framebuffer_info fb_info = get_framebuffer_info("/dev/fb0");
    std::ofstream ofs("/dev/fb0", std::ios::out | std::ios::binary);
    if (!ofs.is_open()) {
        std::cerr << "Error: cannot open /dev/fb0" << std::endl;
        return -1;
    }

    int screen_width  = fb_info.xres_virtual;
    int screen_height = 1080;
    int screen_bpp    = fb_info.bits_per_pixel / 8;

    std::cout << "Framebuffer info: " << screen_width << "x" << screen_height
              << " bpp=" << fb_info.bits_per_pixel << std::endl;

    // ===== 使用 stb_image 載入 PNG =====
    int width, height, channels;
    unsigned char *data = stbi_load("./advance.png", &width, &height, &channels, 3);
    if (!data) {
        std::cerr << "Error: failed to load advance.png" << std::endl;
        return -1;
    }

    cv::Mat image(height, width, CV_8UC3, data);
    cv::cvtColor(image, image, cv::COLOR_RGB2BGR);

    // ===== 將圖片縮放為螢幕等高（保持寬度可超過螢幕）=====
    double scale = (double)screen_height / image.rows;
    int new_width = (int)(image.cols * scale);
    cv::resize(image, image, cv::Size(new_width, screen_height));

    std::cout << "Image resized to: " << new_width << "x" << screen_height << std::endl;

    int offset_x = 0;
    int direction = 1; // 1 = 向左, -1 = 向右
    int max_offset = std::max(0, new_width - screen_width);

    // ===== 主顯示迴圈（自動移動 + 按鍵控制）=====
    while (true) {
        // 顯示目前區塊
        cv::Rect roi(offset_x, 0, screen_width, screen_height);
        cv::Mat visible = image(roi);

        // 轉換為 BGR565 並輸出到 framebuffer
        cv::Mat visible_BGR565;
        cv::cvtColor(visible, visible_BGR565, cv::COLOR_BGR2BGR565);
        for (int y = 0; y < screen_height; y++) {
            ofs.seekp(y * fb_info.xres_virtual * screen_bpp);
            ofs.write(reinterpret_cast<const char*>(visible_BGR565.ptr(y)),
                      screen_width * screen_bpp);
        }

        // 鍵盤控制方向
        int key = getch_noblock();
        if (key == 'q') break;       // 按 q 離開
        if (key == 'j') direction = 1; // j 向右
        if (key == 'l') direction = -1;  // l 向左

        // 自動滾動
        offset_x += direction * 5;
        if (offset_x >= max_offset) direction = -1;
        if (offset_x <= 0) direction = 1;

        usleep(2000); // 約 50 FPS
    }

    stbi_image_free(data);
    ofs.close();
    return 0;
}

// ===== 取得 framebuffer 資訊 =====
struct framebuffer_info get_framebuffer_info(const char *framebuffer_device_path)
{
    struct framebuffer_info fb_info;
    struct fb_var_screeninfo screen_info;

    int fd = open(framebuffer_device_path, O_RDWR);
    if (fd == -1) {
        perror("Error opening framebuffer device");
        exit(1);
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &screen_info)) {
        perror("Error reading screen info");
        close(fd);
        exit(2);
    }

    fb_info.xres_virtual   = screen_info.xres_virtual;
    fb_info.bits_per_pixel = screen_info.bits_per_pixel;

    close(fd);
    return fb_info;
}
