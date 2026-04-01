#include <fcntl.h>
#include <fstream>
#include <linux/fb.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <sys/ioctl.h>
#include <unistd.h>
#include <iostream>

struct framebuffer_info
{
    uint32_t bits_per_pixel;    // framebuffer depth
    uint32_t xres_virtual;      // how many pixel in a row in virtual screen
};

struct framebuffer_info get_framebuffer_info(const char *framebuffer_device_path);

int main(int argc, const char *argv[])
{
    cv::Mat image;
    cv::Size2f image_size;
    
    framebuffer_info fb_info = get_framebuffer_info("/dev/fb0");
    std::ofstream ofs("/dev/fb0", std::ios::out | std::ios::binary);
    if (!ofs.is_open()) {
        std::cerr << "Error: cannot open /dev/fb0" << std::endl;
        return -1;
    }

    // read image file (sample.bmp) from opencv libs.
    image = cv::imread("NYCU_logo.bmp");
    if (image.empty()) {
        std::cerr << "Error: cannot open NYCU_logo.bmp" << std::endl;
        return -1;
    }

    // get image size of the image.
    image_size = image.size();

    // transfer color space from BGR to BGR565 (16-bit image) to fit the requirement of the LCD
    cv::Mat image_BGR565;
    cv::cvtColor(image, image_BGR565, cv::COLOR_BGR2BGR565);

    // output to framebuffer row by row
    for (int y = 0; y < image_size.height; y++)
    {
        // move to the next written position of output device framebuffer
        ofs.seekp(y * fb_info.xres_virtual * fb_info.bits_per_pixel / 8);

        // write to the framebuffer
        ofs.write(reinterpret_cast<const char*>(image_BGR565.ptr(y)),
                  image_size.width * fb_info.bits_per_pixel / 8);
    }

    ofs.close();
    return 0;
}

struct framebuffer_info get_framebuffer_info(const char *framebuffer_device_path)
{
    struct framebuffer_info fb_info;        // Used to return the required attrs.
    struct fb_var_screeninfo screen_info;   // Used to get attributes of the device from OS kernel.

    // open device
    int fd = open(framebuffer_device_path, O_RDWR);
    if (fd == -1) {
        perror("Error opening framebuffer device");
        exit(1);
    }

    // get attributes
    if (ioctl(fd, FBIOGET_VSCREENINFO, &screen_info)) {
        perror("Error reading screen info");
        close(fd);
        exit(2);
    }

    // put the required attributes in variable fb_info
    fb_info.xres_virtual   = screen_info.xres_virtual;
    fb_info.bits_per_pixel = screen_info.bits_per_pixel;

    close(fd);
    return fb_info;
}
