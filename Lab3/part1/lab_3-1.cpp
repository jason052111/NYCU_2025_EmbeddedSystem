#include <iostream>
#include <opencv2/opencv.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/face.hpp>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <cstring>   // for memset
#include <cerrno>    // for errno

using namespace std;
using namespace cv;
using namespace cv::face;

//====================================================
// 非阻塞讀鍵盤（適用無視窗 framebuffer 環境）
//====================================================
int getch_noblock() {
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 0;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);

    int ch = -1;
    int ret = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    if (ret > 0) ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}

//====================================================
// Framebuffer 資訊結構
//====================================================
struct fb_info {
    int xres;
    int yres;
    int bits_per_pixel;
    long int screensize;
};

// 取得 framebuffer 資訊
int get_framebuffer_info(const char *fb_path, fb_info &info) {
    int fb_fd = open(fb_path, O_RDWR);
    if (fb_fd == -1) {
        perror("Error opening framebuffer device");
        return -1;
    }
    struct fb_var_screeninfo vinfo;
    memset(&vinfo, 0, sizeof(vinfo));
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo)) {
        perror("Error reading variable information");
        close(fb_fd);
        return -1;
    }
    info.xres = vinfo.xres;
    info.yres = vinfo.yres;
    info.bits_per_pixel = vinfo.bits_per_pixel;
    info.screensize = (long int)info.xres * (long int)info.yres * (long int)info.bits_per_pixel / 8;
    close(fb_fd);
    return 0;
}

//====================================================
// 主程式
//====================================================
int main() {
    // 1) 開啟 framebuffer
    const char *fb_path = "/dev/fb0";
    fb_info fbInfo;
    if (get_framebuffer_info(fb_path, fbInfo) == -1) return -1;

    int fb_fd = open(fb_path, O_RDWR);
    if (fb_fd == -1) {
        perror("Error: cannot open framebuffer device");
        return -1;
    }

    unsigned char *fb_ptr = (unsigned char *)mmap(0, fbInfo.screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_ptr == MAP_FAILED) {
        perror("Error: failed to map framebuffer device to memory");
        close(fb_fd);
        return -1;
    }

    // 2) 開啟攝影機
    VideoCapture cap(2);
    if (!cap.isOpened()) {
        cerr << "Error: cannot open camera" << endl;
        munmap(fb_ptr, fbInfo.screensize);
        close(fb_fd);
        return -1;
    }
    cap.set(CAP_PROP_FRAME_WIDTH, 640);
    cap.set(CAP_PROP_FRAME_HEIGHT, 480);
    cap.set(CAP_PROP_BUFFERSIZE, 1); // 只保留最新一幀，降低延遲

    // 3) 載入人臉偵測分類器（Haar）
    CascadeClassifier face_cascade;
    if (!face_cascade.load("./haarcascade_frontalface_default.xml")) {
        cerr << "can't load Haar model" << endl;
        munmap(fb_ptr, fbInfo.screensize);
        close(fb_fd);
        return -1;
    }

    // 4) 載入 LBPH 辨識模型（opencv_contrib/face）
    Ptr<LBPHFaceRecognizer> model = LBPHFaceRecognizer::create();
    bool model_ok = false;
    try {
        model->read("./lbph_model.yml"); // 依實際路徑調整
        cout << "loaded lbph_model.yml" << endl;
        model_ok = true;
    } catch (cv::Exception &e) {
        cerr << "can't load lbph_model.yml, recognition will be skipped." << endl;
        model.release(); // 等效 nullptr
        model_ok = false;
    }

    // 5) 主迴圈
    Mat frame, resized, frame_BGR565;
    for (;;) {
        cap >> frame;
        if (frame.empty()) break;

        // 轉灰階
        Mat gray;
        cvtColor(frame, gray, COLOR_BGR2GRAY);

        // 偵測人臉
        vector<Rect> faces;
        face_cascade.detectMultiScale(gray, faces, 1.1, 5, 0, Size(80, 80));

        // 辨識並畫框（改用舊式 for，避免 C++11 依賴）
        for (size_t i = 0; i < faces.size(); ++i) {
            Rect f = faces[i];
            string text = "unknown";
            if (model_ok) {
                Mat faceROI = gray(f);
                int label = -1;
                double conf = 1e9;
                model->predict(faceROI, label, conf);
                // 門檻可依實測調整（LBPH: conf 越小越好）
                if (conf < 80.0) {
                    if (label == 1) text = "314551147";
                    else if (label == 2) text = "112550052";
                    else text = "ID " + to_string(label);
                }
            }
            rectangle(frame, f, Scalar(0, 255, 0), 2);
            putText(frame, text, Point(f.x, f.y - 10),
                    FONT_HERSHEY_SIMPLEX, 0.8, Scalar(0, 255, 0), 2);
        }

        // 縮放到 framebuffer 大小
        resize(frame, resized, Size(fbInfo.xres, fbInfo.yres));

        // 轉成 BGR565 並輸出到螢幕
        cvtColor(resized, frame_BGR565, COLOR_BGR2BGR565);
        memcpy(fb_ptr, frame_BGR565.data, fbInfo.screensize);

        // 非阻塞鍵盤：按 'q' 離開
        int key = getch_noblock();
        if (key == 'q' || key == 'Q' || key == 27) break;

        // 小睡一下降低 CPU 佔用
        usleep(10000); // 10ms
    }

    // 收尾
    munmap(fb_ptr, fbInfo.screensize);
    close(fb_fd);
    return 0;
}
