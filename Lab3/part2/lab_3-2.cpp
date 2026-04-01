// yolov3_helmet.cpp
// 使用 OpenCV 3.4.7 dnn 模組做 YOLOv3 安全帽偵測

#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>

int main(int argc, char** argv)
{
    // ============== 可調參數 ==============

    // 這三個路徑請改成你板子上的實際路徑或放同資料夾用相對路徑
    std::string cfgPath     = "./yolov3-helmet.cfg";
    std::string weightsPath = "./yolov3-helmet_final.weights";
    std::string namesPath   = "./obj.names";

    // 測試圖片：如果有傳參數就用 argv[1]，沒有就用預設 test.jpg
    std::string imagePath   = (argc > 1) ? argv[1] : "./testcase.jpg";
    std::string outputPath  = "./output_helmet.jpg";

    // 門檻
    float confThreshold = 0.05f;  // 信心門檻
    float nmsThreshold  = 0.50f;  // NMS IoU 門檻

    // 要跟 cfg 裡 [net] 的 width / height 一樣
    int inputWidth  = 416;
    int inputHeight = 416;

    bool DEBUG = true;

    // ============== 讀取類別名稱 ==============

    std::vector<std::string> classes;
    std::ifstream ifs(namesPath.c_str());
    if (!ifs.is_open()) {
        std::cerr << "Error: cannot open names file: " << namesPath << std::endl;
        return -1;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty())
            classes.push_back(line);
    }
    ifs.close();

    // ============== 載入網路（Darknet cfg + weights） ==============

    cv::dnn::Net net;
    try {
        net = cv::dnn::readNetFromDarknet(cfgPath, weightsPath);
    } catch (const cv::Exception& e) {
        std::cerr << "Error loading network: " << e.msg << std::endl;
        return -1;
    }

    if (net.empty()) {
        std::cerr << "Error: net is empty, check cfg/weights path." << std::endl;
        return -1;
    }

    // 如果板子有 OpenCV CUDA 版本，可以打開這兩行
    // net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
    // net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);

    // ============== 讀圖 + 建 blob ==============

    cv::Mat img = cv::imread(imagePath);
    if (img.empty()) {
        std::cerr << "Error: cannot read image: " << imagePath << std::endl;
        return -1;
    }

    int H = img.rows;
    int W = img.cols;

    cv::Mat blob = cv::dnn::blobFromImage(
        img,
        1.0 / 255.0,
        cv::Size(inputWidth, inputHeight),
        cv::Scalar(),   // mean = (0,0,0)
        true,           // swapRB
        false           // crop
    );
    net.setInput(blob);

    // ============== 找輸出層名稱（相容 OpenCV 3.4.x） ==============

    std::vector<cv::String> layerNames = net.getLayerNames();
    std::vector<int> outLayers = net.getUnconnectedOutLayers();

    std::vector<cv::String> outNames;
    outNames.reserve(outLayers.size());
    for (size_t i = 0; i < outLayers.size(); ++i) {
        int idx = outLayers[i];          // 1-based
        outNames.push_back(layerNames[idx - 1]);
    }

    if (DEBUG) {
        std::cout << "Output layers: ";
        for (size_t i = 0; i < outNames.size(); ++i) {
            std::cout << outNames[i];
            if (i + 1 < outNames.size()) std::cout << ", ";
        }
        std::cout << std::endl;
    }

    // ============== forward ==============

    std::vector<cv::Mat> outs;
    net.forward(outs, outNames);

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> classIds;

    for (size_t i = 0; i < outs.size(); ++i) {
        cv::Mat& out = outs[i];
        // 每一列代表一個 detection: [cx, cy, w, h, obj_conf, cls0, cls1, ...]
        for (int j = 0; j < out.rows; ++j) {
            float* data = (float*)out.ptr(j);
            float obj_conf = data[4];

            cv::Mat scores = out.row(j).colRange(5, out.cols);
            cv::Point classIdPoint;
            double classScore;
            cv::minMaxLoc(scores, nullptr, &classScore, nullptr, &classIdPoint);

            float class_conf = static_cast<float>(classScore);
            float confidence = obj_conf * class_conf;  // YOLO 通常 obj_conf * class_conf

            if (confidence > confThreshold) {
                float cx = data[0] * static_cast<float>(W);
                float cy = data[1] * static_cast<float>(H);
                float w  = data[2] * static_cast<float>(W);
                float h  = data[3] * static_cast<float>(H);

                int left   = static_cast<int>(cx - w / 2.0f);
                int top    = static_cast<int>(cy - h / 2.0f);
                int width  = static_cast<int>(w);
                int height = static_cast<int>(h);

                boxes.push_back(cv::Rect(left, top, width, height));
                confidences.push_back(confidence);
                classIds.push_back(classIdPoint.x);
            }
        }
    }

    if (DEBUG) {
        std::cout << "raw detections (after conf_thresh): " << boxes.size() << std::endl;
        if (!confidences.empty()) {
            float cmin = confidences[0], cmax = confidences[0];
            for (float c : confidences) {
                if (c < cmin) cmin = c;
                if (c > cmax) cmax = c;
            }
            std::cout << "conf range: min = " << cmin << " max = " << cmax << std::endl;
        } else {
            std::cout << "no boxes passed conf_thresh" << std::endl;
        }
    }

    // ============== NMS ==============

    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences, confThreshold, nmsThreshold, indices);

    if (DEBUG) {
        std::cout << "after NMS, keep: " << indices.size() << std::endl;
    }

    if (indices.empty()) {
        std::cout << "No detections kept after NMS." << std::endl;
    }

    // ============== 畫框並存檔 ==============

    for (int idx : indices) {
        cv::Rect box = boxes[idx];
        int clsId = classIds[idx];
        std::string label;
        if (clsId >= 0 && clsId < (int)classes.size())
            label = classes[clsId];
        else
            label = std::to_string(clsId);

        float conf = confidences[idx];

        cv::rectangle(img, box, cv::Scalar(0, 255, 0), 2);

        char text[256];
        std::snprintf(text, sizeof(text), "%s %.2f", label.c_str(), conf);
        int baseLine = 0;
        cv::Size labelSize = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);

        int top = std::max(box.y, labelSize.height);
        cv::putText(img, text, cv::Point(box.x, top - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
    }

    if (!cv::imwrite(outputPath, img)) {
        std::cerr << "Error: failed to write output image: " << outputPath << std::endl;
        return -1;
    }

    std::cout << "Saved result to " << outputPath << std::endl;
    return 0;
}
