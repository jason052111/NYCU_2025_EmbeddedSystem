# LAB3 Report: Face Recognition and Helmet Detection

---

## 1. Recognition Algorithm Design

### 1.1 Part 1: Face Recognition with LBPH
In Part 1, I implemented a simple face recognition system based on OpenCV and the LBPH (Local Binary Patterns Histograms) algorithm.

First, I recompiled OpenCV 3.4.7 with the `opencv_contrib` modules enabled on the embedded platform. This allowed me to use the built-in face detection and LBPH face recognizer provided by the `face` module.

For face detection, I used the OpenCV (contrib) face detector to locate all faces in the input frames. For each detected face, the corresponding region of interest is cropped, converted to grayscale, and resized to a fixed resolution.

For recognition, I used the LBPH face recognizer. During training, the LBPH model learns a feature representation from multiple samples of each known person. During inference, each detected face is converted to the same LBPH feature space and compared to the stored templates. If the distance to the closest template is below a threshold, the system outputs the corresponding student ID; otherwise, the face is treated as "unknown". In this way, Part 1 uses a combination of OpenCV face detection and LBPH feature matching to perform face recognition.

### 1.2 Part 2: Helmet Detection with YOLOv3
In Part 2, I switched to an object detection approach and designed a helmet detector based on YOLOv3. The goal is to detect only one class, *helmet*, instead of the 80 classes in the original COCO model.

The provided dataset uses VOC-style XML annotations (`xmin`, `ymin`, `xmax`, `ymax`). I first converted these annotations into YOLO format: for each bounding box, I computed the normalized center coordinates and size (x, y, w, h) with respect to the image width and height. During this conversion, I also checked for invalid boxes (e.g., coordinates out of range) to avoid corrupt labels.

Next, I used the Darknet tool `detector calc_anchors` to recompute nine anchor boxes from my helmet dataset. These anchors were then written back into the three `[yolo]` layers in the YOLOv3 configuration file, so that the anchor sizes better match the typical shape and scale of helmets in the training images.

In the YOLOv3 configuration (`yolov3-helmet.cfg`), I modified:
* `classes` in each `[yolo]` layer to 1 (only helmet).
* The last convolutional layer before each `[yolo]` layer to have `filters = (classes + 5) * 3 = 18`.
* The input resolution to `width = 416`, `height = 416`, and suitable training parameters such as `batch`, `subdivisions`, and `max_batches`.

Using this customized configuration, I fine-tuned YOLOv3 on the helmet dataset starting from the official `yolov3.weights`. After training on a GPU PC, I obtained `yolov3-helmet_final.weights`, which is a single-class helmet detector.

---

## 2. System Operation

### 2.1 Part 1: Face Recognition Pipeline
The runtime pipeline for Part 1 works as follows:

1. The embedded board captures frames from a camera.
2. For each frame, the OpenCV (contrib) face detector finds all face regions.
3. Each detected face is cropped, converted to grayscale, resized, and passed to the LBPH recognizer.
4. The LBPH model outputs the closest known identity and a distance score. If the distance is within a preset threshold, the corresponding student ID is displayed; otherwise, the face is labeled as "unknown".
5. Bounding boxes and labels are drawn on the frame and displayed on the screen.

### 2.2 Part 2: Helmet Detection Pipeline
The runtime pipeline for Part 2 is built on top of the trained YOLOv3 helmet model and OpenCV's `dnn` module:

1. On the embedded board, install OpenCV 3.4.7 (with the `dnn` module) and copy `yolov3-helmet.cfg`, `yolov3-helmet_final.weights`, and `obj.names` (which contains the single class name "helmet") to the board.
2. A C++ program uses `cv::dnn::readNetFromDarknet` to load the YOLOv3 helmet model.
3. The program reads a test JPEG image from the file system using `cv::imread`.
4. The image is converted into a blob using `blobFromImage` (resize to 416x416, RGB swap, normalization) and passed through the network via `net.forward`.
5. For each detection, the system computes a confidence score (objectness * class score). Detections below a confidence threshold are discarded, and Non-Maximum Suppression (NMS) is applied to remove overlapping boxes.
6. The remaining detections are drawn as bounding boxes with the "helmet" label and confidence score on the image.
7. Finally, the resulting image is saved as an output JPEG file for later inspection, and concurrently converted to RGB565 to be written row by row to `/dev/fb0` (displaying directly on the LCD via the Linux framebuffer without any GUI library).

> **Summary:** Part 1 uses OpenCV's face detection together with LBPH for identity recognition, while Part 2 uses a customized single-class YOLOv3 model for helmet detection. Both parts are implemented to run efficiently on the embedded platform using OpenCV and the Linux framebuffer.
