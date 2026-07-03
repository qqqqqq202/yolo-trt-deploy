#pragma once

#include "engine.h"

#include <opencv2/core.hpp>
#include <vector>

struct Detection {
    cv::Rect2f box;   ///< bounding box in image coordinates (x, y, w, h)
    float     conf;   ///< confidence score [0, 1]
    int       klass;  ///< COCO class index (0 = person)
};

/// YOLOv8 detector running on TensorRT.
class Detector {
public:
    /// @param engine  Ready‑to‑use Engine instance
    /// @param confThr Confidence threshold for keeping a detection
    /// @param nmsThr  IoU threshold for NMS
    Detector(Engine& engine, float confThr = 0.45f, float nmsThr = 0.45f);
    ~Detector();

    /// Run detection on one BGR image frame.
    std::vector<Detection> detect(const cv::Mat& bgr);

    int inputSize()  const { return _inputSize; }
    int numClasses() const { return _numClasses; }
    int numAnchors() const { return _numAnchors; }

private:
    cv::Mat preprocess(const cv::Mat& bgr);
    std::vector<Detection> postprocess(float* output, const cv::Size& originalSize);

    Engine& _engine;
    float   _confThr;
    float   _nmsThr;

    int _inputSize   = 640;
    int _numClasses  = 80;
    int _numAnchors  = 8400;

    // GPU buffers
    float* _dInput  = nullptr;
    float* _dOutput = nullptr;

    // CUDA stream
    cudaStream_t _stream = nullptr;
};
