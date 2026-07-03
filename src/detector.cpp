#include "detector.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>

// -----------------------------------------------------------------------------
// Helper: letterbox resize
// -----------------------------------------------------------------------------
static cv::Mat letterbox(const cv::Mat& src, int targetSize,
                         float& scale, int& padLeft, int& padTop) {
    int w = src.cols, h = src.rows;
    scale = std::min(static_cast<float>(targetSize) / w,
                     static_cast<float>(targetSize) / h);

    int newW = static_cast<int>(w * scale);
    int newH = static_cast<int>(h * scale);
    padLeft = (targetSize - newW) / 2;
    padTop  = (targetSize - newH) / 2;

    cv::Mat resized;
    cv::resize(src, resized, cv::Size(newW, newH), 0, 0, cv::INTER_LINEAR);
    cv::Mat padded(targetSize, targetSize, src.type(), cv::Scalar(114, 114, 114));
    resized.copyTo(padded(cv::Rect(padLeft, padTop, newW, newH)));

    return padded;
}

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
Detector::Detector(Engine& engine, float confThr, float nmsThr)
    : _engine(engine), _confThr(confThr), _nmsThr(nmsThr) {

    cudaStreamCreate(&_stream);

    // Get tensor shapes (TRT 11 uses tensor names)
    auto inputShape  = _engine.engine().getTensorShape(_engine.inputName());
    auto outputShape = _engine.engine().getTensorShape(_engine.outputName());

    // input:  [1, 3, H, W]   output: [1, 84, N]
    _inputSize  = inputShape.d[2];
    _numAnchors = outputShape.d[2];
    _numClasses = outputShape.d[1] - 4;

    std::cout << "[Detector] Input:  " << inputShape.d[0] << "x" << inputShape.d[1]
              << "x" << inputShape.d[2] << "x" << inputShape.d[3] << std::endl;
    std::cout << "[Detector] Output: " << outputShape.d[0] << "x" << outputShape.d[1]
              << "x" << outputShape.d[2] << std::endl;
    std::cout << "[Detector] confThr=" << _confThr << " nmsThr=" << _nmsThr << std::endl;

    // Allocate GPU buffers
    size_t inputBytes  = 1 * 3 * _inputSize * _inputSize * sizeof(float);
    size_t outputBytes = 1 * (_numClasses + 4) * _numAnchors * sizeof(float);

    cudaMalloc(reinterpret_cast<void**>(&_dInput),  inputBytes);
    cudaMalloc(reinterpret_cast<void**>(&_dOutput), outputBytes);
}

Detector::~Detector() {
    if (_dInput)  cudaFree(_dInput);
    if (_dOutput) cudaFree(_dOutput);
    if (_stream)  cudaStreamDestroy(_stream);
}

// -----------------------------------------------------------------------------
// Preprocess: BGR → letterbox → RGB → CHW → /255.0
// Returns a flat float Mat (1 × N) with [RGB_data, scale, padLeft, padTop, 0]
// -----------------------------------------------------------------------------
cv::Mat Detector::preprocess(const cv::Mat& bgr) {
    float scale;
    int padLeft, padTop;
    cv::Mat padded = letterbox(bgr, _inputSize, scale, padLeft, padTop);

    cv::Mat rgb;
    cv::cvtColor(padded, rgb, cv::COLOR_BGR2RGB);
    rgb.convertTo(rgb, CV_32FC3, 1.0 / 255.0);

    // HWC → CHW and flatten
    std::vector<cv::Mat> channels(3);
    cv::split(rgb, channels);

    cv::Mat flat(1, 3 * _inputSize * _inputSize + 4, CV_32FC1);
    float* ptr = flat.ptr<float>();
    for (int c = 0; c < 3; ++c) {
        std::memcpy(ptr, channels[c].ptr<float>(),
                    _inputSize * _inputSize * sizeof(float));
        ptr += _inputSize * _inputSize;
    }
    // Append meta: scale, padLeft, padTop, 0
    ptr[0] = scale;
    ptr[1] = static_cast<float>(padLeft);
    ptr[2] = static_cast<float>(padTop);
    ptr[3] = 0.0f;

    return flat;
}

// -----------------------------------------------------------------------------
// Detect
// -----------------------------------------------------------------------------
std::vector<Detection> Detector::detect(const cv::Mat& bgr) {
    // 1. Preprocess on CPU
    cv::Mat flat = preprocess(bgr);

    int N = flat.cols - 4;
    float* meta = flat.ptr<float>() + N;
    float scale   = meta[0];
    float padLeft = meta[1];
    float padTop  = meta[2];

    // 2. Copy input to GPU
    size_t inputBytes = N * sizeof(float);
    cudaMemcpyAsync(_dInput, flat.ptr<float>(), inputBytes,
                    cudaMemcpyHostToDevice, _stream);

    // 3. Set tensor addresses (TRT 11)
    _engine.context().setTensorAddress(_engine.inputName(), _dInput);
    _engine.context().setTensorAddress(_engine.outputName(), _dOutput);

    // 4. Run inference
    _engine.context().enqueueV3(_stream);

    // 5. Copy output back
    size_t outputBytes = 1 * (_numClasses + 4) * _numAnchors * sizeof(float);
    std::vector<float> hOutput((_numClasses + 4) * _numAnchors);
    cudaMemcpyAsync(hOutput.data(), _dOutput, outputBytes,
                    cudaMemcpyDeviceToHost, _stream);
    cudaStreamSynchronize(_stream);

    // 6. Postprocess
    auto dets = postprocess(hOutput.data(), bgr.size());

    // 7. Scale boxes from letterbox space → original image
    for (auto& d : dets) {
        d.box.x = (d.box.x - padLeft) / scale;
        d.box.y = (d.box.y - padTop)  / scale;
        d.box.width  /= scale;
        d.box.height /= scale;
    }

    return dets;
}

// -----------------------------------------------------------------------------
// Postprocess: parse YOLOv8 output + per‑class NMS
// -----------------------------------------------------------------------------
std::vector<Detection> Detector::postprocess(float* output, const cv::Size& /*original*/) {
    std::vector<Detection> candidates;
    candidates.reserve(256);

    for (int i = 0; i < _numAnchors; ++i) {
        // Layout: [cx, cy, w, h, cls0, cls1, ..., cls79] per anchor
        float cx = output[0 * _numAnchors + i];
        float cy = output[1 * _numAnchors + i];
        float w  = output[2 * _numAnchors + i];
        float h  = output[3 * _numAnchors + i];

        float maxScore = 0.0f;
        int   bestCls  = -1;
        for (int c = 0; c < _numClasses; ++c) {
            float s = output[(4 + c) * _numAnchors + i];
            if (s > maxScore) {
                maxScore = s;
                bestCls  = c;
            }
        }

        if (maxScore < _confThr) continue;

        float x = cx - w * 0.5f;
        float y = cy - h * 0.5f;
        candidates.push_back({cv::Rect2f(x, y, w, h), maxScore, bestCls});
    }

    // Sort by confidence descending
    std::sort(candidates.begin(), candidates.end(),
              [](const Detection& a, const Detection& b) { return a.conf > b.conf; });

    std::vector<Detection> kept;
    std::vector<bool> suppressed(candidates.size(), false);

    for (size_t i = 0; i < candidates.size(); ++i) {
        if (suppressed[i]) continue;
        kept.push_back(candidates[i]);

        for (size_t j = i + 1; j < candidates.size(); ++j) {
            if (suppressed[j]) continue;
            if (candidates[i].klass != candidates[j].klass) continue;

            float ix1 = std::max(candidates[i].box.x, candidates[j].box.x);
            float iy1 = std::max(candidates[i].box.y, candidates[j].box.y);
            float ix2 = std::min(candidates[i].box.x + candidates[i].box.width,
                                 candidates[j].box.x + candidates[j].box.width);
            float iy2 = std::min(candidates[i].box.y + candidates[i].box.height,
                                 candidates[j].box.y + candidates[j].box.height);
            float inter = std::max(0.0f, ix2 - ix1) * std::max(0.0f, iy2 - iy1);

            float areaA = candidates[i].box.width * candidates[i].box.height;
            float areaB = candidates[j].box.width * candidates[j].box.height;
            float iou = inter / (areaA + areaB - inter + 1e-6f);

            if (iou > _nmsThr) suppressed[j] = true;
        }
    }

    return kept;
}
