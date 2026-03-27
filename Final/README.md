# Final Project Report: Embedded Object Detection System

---

## 1. Introduction
The objective of this project is to implement an embedded object detection system using the **E9V3 development board**. The project is divided into two main tasks: **Real-Time Object Recognition** and **Photo Object Recognition**.

To achieve a balance between inference speed and detection accuracy on an embedded device, we utilized the **NCNN** high-performance neural network inference framework optimized for mobile platforms, integrated with **OpenCV** for image pre-processing and post-processing.

---

## 2. Environment & Library Build
To execute deep learning models on the E9V3 board, we cross-compiled the OpenCV and NCNN libraries from the source code.

### 2.1 OpenCV Build
* **Version:** 3.4.7
* **Configuration:**
  * **Modules:** Included `opencv_contrib`.
  * **DNN:** Enabled (although NCNN is the primary engine).
  * **Codecs:** Enabled `PROTOBUF` and `JPEG` support for handling models and image files.
  * **GUI:** Enabled `Qt` support for potential display requirements.

### 2.2 NCNN Build Process
NCNN is the core inference engine for this project. We configured it specifically for the ARM architecture.

#### Toolchain Configuration
In `toolchain-e9v3-ncnn.cmake`, we defined the cross-compiler paths for the E9V3 board:

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER   "/opt/EmbedSky/gcc-linaro-5.3-2016.02-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc")
set(CMAKE_CXX_COMPILER "/opt/EmbedSky/gcc-linaro-5.3-2016.02-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-g++")
# ... (library and include paths)
```

#### CMake Configuration & Build
To maximize performance on the multi-core CPU, we explicitly enabled **OpenMP** (`-DNCNN_OPENMP=ON`).

```bash
/usr/bin/cmake -H/root/ncnn -B/root/work_ncnn/build-e9v3-so \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/root/work_ncnn/toolchain-e9v3-ncnn.cmake \
  -DNCNN_SHARED_LIB=ON \
  -DNCNN_OPENMP=ON \      # Critical for multi-core acceleration
  -DNCNN_VULKAN=OFF \
  -DNCNN_INSTALL_SDK=ON \
  -DTHREADS_HAVE_PTHREAD_ARG=1 \
  -DTHREADS_PTHREAD_ARG=2
```
Finally, the library was built and installed using `cmake --build ... --target install`.

---

## 3. System Implementation

### 3.1 Part 1: Real-Time Object Recognition
* **Goal:** Achieve low-latency detection (< 1 second per frame).
* **Model Strategy:** **YOLOv8n (Nano)**
  * We selected the smallest model (Nano) to minimize computational load on the E9V3 hardware.

**Implementation Details (`realtime_inference.cpp`):**
1. **Input:** Captured video via OpenCV `VideoCapture`.
2. **Preprocessing:** Resized input frames to **320x320** (`TARGET_SIZE`). This resolution trade-off significantly boosts inference speed while maintaining sufficient accuracy for the required objects.
3. **Display Optimization:** Writing directly to the Framebuffer (`/dev/fb0`) to bypass the overhead of a window manager.
4. **Filtering:** The system is programmed to only display and bound the 8 specific required classes (Spoon, Banana, Keyboard, Cell phone, Book, Scissors, Bottle, Cup).

### 3.2 Part 2: Photo Object Recognition
* **Goal:** Maximize detection accuracy for hidden objects.
* **Model Strategy:** **Ensemble Approach (YOLOv8x + YOLOv8s)** We utilized two distinct models to ensure robust detection:
  * **YOLOv8x (X-Large):** A powerful model pre-trained on the COCO dataset (Input size 640). It handles general object detection with high accuracy.
  * **YOLOv8s (Small - Custom Trained):** A model explicitly trained by our team to target specific objects that required higher precision or were part of the custom dataset.

**Custom Trained Classes (YOLOv8s):**
We collected data and trained the YOLOv8s model specifically to recognize the following items with high confidence:
* Dart
* Pencil
* Poker card
* Sticky note
* Tissue

**Implementation Details (`photoobject_inference.cpp`):**
The system runs a two-stage inference. First, the YOLOv8x detects standard COCO classes. Second, the custom YOLOv8s model (running at a higher resolution of **960x960** for detail) detects the specific trained items. The results are merged to produce the final output image.

---

## 4. Compilation & Deployment
To deploy the software, we used the `arm-linux-gnueabihf-g++` cross-compiler. The compilation command links the NCNN engine, OpenCV libraries, and enables OpenMP for threading support.

**Compilation Command:**
```bash
arm-linux-gnueabihf-g++ main_new.cpp -o inference \
   -I /opt/EmbedSky/gcc-linaro-5.3-2016.02-x86_64_arm-linux-gnueabihf/include/ \
   -I /usr/local/arm-opencv/install/include/ \
   -I /root/work_ncnn/install-e9v3-so-omp/include/ \
   -L /usr/local/arm-opencv/install/lib/ \
   -L /root/work_ncnn/install-e9v3-so-omp/lib/ \
   -Wl,-rpath-link=/opt/EmbedSky/gcc-linaro-5.3-2016.02-x86_64_arm-linux-gnueabihf/arm-linux-gnueabihf/libc/lib/ \
   -Wl,-rpath-link=/opt/EmbedSky/gcc-linaro-5.3-2016.02-x86_64_arm-linux-gnueabihf/qt5.5/rootfs_imx6q_V3_qt5.5_env/lib/ \
   -Wl,-rpath-link=/opt/EmbedSky/gcc-linaro-5.3-2016.02-x86_64_arm-linux-gnueabihf/qt5.5/rootfs_imx6q_V3_qt5.5_env/qt5.5_env/lib/ \
   -Wl,-rpath-link=/opt/EmbedSky/gcc-linaro-5.3-2016.02-x86_64_arm-linux-gnueabihf/qt5.5/rootfs_imx6q_V3_qt5.5_env/usr/lib/ \
   -lpthread -lncnn -lopencv_world -fopenmp -std=c++11
```

---

## 5. Conclusion
This project successfully implemented a deep learning-based object detection system on the E9V3 platform. By strategically selecting models—**YOLOv8n** for real-time speed and a **hybrid YOLOv8x/YOLOv8s** for high-precision static recognition—we fulfilled the requirements of fast inference and accurate multi-class detection within the constraints of an embedded environment.
