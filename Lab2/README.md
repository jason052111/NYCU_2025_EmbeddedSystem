# LAB2 Report: Video Output and Input via Framebuffer

---

## 1. Introduction & Objectives
This lab aims to implement video output and input functions on an embedded system (E9V3) without relying on the X Window System or any GUI frameworks. Instead, it directly manipulates the Linux Framebuffer (`/dev/fb0`) for rendering. The experiment is divided into three parts:
1. **Static Image Display** (Outputting a BMP to the framebuffer).
2. **Real-Time Camera Streaming & Non-blocking Screenshots**.
3. **HDMI 1080p Output & Electronic Scroll Board Implementation**.

---

## 2. System Implementation

### 2.1 Part 1: Static Image Display (`lab_2-1.cpp`)
* **Goal:** Display the provided `NYCU_logo.bmp` on the development board's screen.
* **Implementation Details:** * Open the framebuffer device using `open("/dev/fb0", O_RDWR)`.
  * Use `ioctl` with `FBIOGET_VSCREENINFO` to retrieve the screen's resolution (`xres_virtual`) and color depth (`bits_per_pixel`).
  * After reading the image using OpenCV, convert the color space from the original BGR to the 16-bit **BGR565** format supported by the LCD.
  * Write the converted pixel data row by row into the corresponding memory address of the framebuffer to display it.

### 2.2 Part 2: Real-time Camera Stream & Screenshots (`lab_2-2.cpp`)
* **Goal:** Display the camera stream while maintaining the aspect ratio (without distortion), and implement a non-blocking screenshot function using the `c` key.
* **Letterboxing (Centering and Padding):** After capturing the camera frame, calculate the aspect ratios of the source frame and the target screen. Scale the frame proportionally and use `cv::copyMakeBorder` to add black borders to the top/bottom or left/right, ensuring the image is strictly centered and undistorted.
* **Non-blocking Keyboard Input:** Implemented `getch_noblock()` by using `termios` to disable terminal line buffering and echo. It uses `select()` to monitor `STDIN_FILENO`, enabling key detection without blocking the main loop.
* **Screenshot Saving:** Upon startup, the program automatically creates incrementally named `screenshot_{id}` directories. When the `c` key is pressed, it directly saves the original frame as a BMP file via OpenCV, fulfilling the zero-delay screenshot requirement.

### 2.3 Part 3: HDMI Output & Electronic Scroll Board (`lab_2-3_advance.cpp`)
* **Goal:** Display `advance.png` via HDMI (1080p) output and implement an auto-scrolling marquee controlled by the keyboard.
* **Lightweight Image Loading:** To avoid heavy dependencies for the advanced requirements, `stb_image.h` was introduced to replace some OpenCV functions for loading PNG images.
* **Scrolling Logic:** * Scale the loaded image proportionally to the screen height (1080). At this point, the image width will exceed the screen width.
  * Set an `offset_x` variable as the starting point of the Region of Interest (ROI). In the main loop, continuously extract the image block from `offset_x` to `offset_x + screen_width` and write it to the framebuffer.
  * In each iteration, automatically increment `offset_x` by the directional offset, and use `getch_noblock()` to monitor the `j` (right) and `l` (left) keys to change the scrolling direction dynamically.

---

## 3. Compilation Commands
```bash
# Cross-compiling the application with OpenCV and pthread
arm-linux-gnueabihf-g++ lab_2-3_advance.cpp -o demo \
  -I/opt/EmbedSky/gcc-linaro-5.3-2016.02-x86_64_arm-linux-gnueabihf/include/ \
  -I/usr/local/arm-opencv/install/include/ \
  -L/usr/local/arm-opencv/install/lib/ \
  -Wl,-rpath-link=/opt/EmbedSky/gcc-linaro-5.3-2016.02-x86_64_arm-linux-gnueabihf/arm-linux-gnueabihf/libc/lib/ \
  -Wl,-rpath-link=/opt/EmbedSky/gcc-linaro-5.3-2016.02-x86_64_arm-linux-gnueabihf/qt5.5/rootfs_imx6q_V3_qt5.5_env/lib/ \
  -Wl,-rpath-link=/opt/EmbedSky/gcc-linaro-5.3-2016.02-x86_64_arm-linux-gnueabihf/qt5.5/rootfs_imx6q_V3_qt5.5_env/qt5.5_env/lib/ \
  -Wl,-rpath-link=/opt/EmbedSky/gcc-linaro-5.3-2016.02-x86_64_arm-linux-gnueabihf/qt5.5/rootfs_imx6q_V3_qt5.5_env/usr/lib/ \
  -lpthread -lopencv_world
```
*Note: Specify the dynamic library path before execution:* `LD_LIBRARY_PATH=./ /path/to/demo`

---

## 4. Questions and Conceptual Answers

| Question | Answer |
| :--- | :--- |
| **Q1: What are cmake and make for? Relationship?** | `cmake` is a cross-platform build system generator that reads `CMakeLists.txt` and generates the corresponding Makefile for the target platform. `make` reads the generated Makefile and invokes the compiler (e.g., gcc/g++) to compile the source code and link the libraries. |
| **Q2: Why are there so many arguments in compilation?** | These arguments specify paths for the cross-compiler to find header files (`-I`) and static/dynamic libraries (`-L`). `-Wl,-rpath-link` tells the linker where to resolve dynamic library dependencies during compile time, ensuring the ARM-architecture program links correctly on the x86 host. |
| **Q3: What is libopencv_world.so? Why LD_LIBRARY_PATH?** | `libopencv_world.so` is a dynamic library that bundles all OpenCV modules together. `LD_LIBRARY_PATH` is used to tell the Linux Loader (`ld.so`) where to find this dynamic library at runtime. Without it, the system defaults to searching `/lib` and `/usr/lib`, causing execution failure due to missing libraries. |
| **Q4: Why not just use cv::imshow()?** | `cv::imshow()` heavily relies on windowing systems (like X Window or Wayland) and GUI frameworks (like GTK/Qt). Stripped-down embedded Linux environments usually lack these heavy windowing systems, making it necessary to write pixel data directly to the framebuffer to display images. |
| **Q5: What is a framebuffer?** | The Framebuffer is a subsystem provided by the Linux kernel that abstracts graphics hardware into a memory space (Video Memory). Developers simply write pixel color data into this memory, and the display controller automatically outputs it to the screen. |
| **Q6: `cat /dev/fb0 > fb0` then `cat fb0 > /dev/fb0`?** | The first command backs up the current pixel data on the screen to a file named `fb0` (equivalent to a full-screen screenshot). The second command writes that file's data back to the framebuffer, restoring the screen to the exact state when the screenshot was taken. |
| **Q7: Difference between `/dev/fb0` and `/dev/fb1`?** | On many development boards (like i.MX6), the SOC supports multiple independent display interfaces (e.g., LCD screen and HDMI output). `/dev/fb0` typically corresponds to the default primary display (like LCD), while `/dev/fb1` corresponds to the secondary display interface (like HDMI). |

---

## 5. References
* 114 Embedded System Design LAB 2 Manual
* OpenCV 3.4.7 Documentation
* Linux Framebuffer API (`<linux/fb.h>`)
* stb_image: Single-file image reading library
