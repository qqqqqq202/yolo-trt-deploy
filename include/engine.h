#pragma once

#include <memory>
#include <string>
#include <vector>

#include <NvInfer.h>
#include <cuda_runtime_api.h>

/// RAII wrapper around a TensorRT engine + execution context.
/// First run: builds engine from ONNX, serializes to .engine file.
/// Subsequent runs: loads directly from .engine file.
class Engine {
public:
    /// @param onnxPath   Path to the ONNX model file
    /// @param enginePath Path to cache the serialized engine; auto‑generated from onnxPath when empty.
    explicit Engine(const std::string& onnxPath, std::string enginePath = "");

    ~Engine();

    // Non‑copyable
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    nvinfer1::ICudaEngine&       engine()  { return *_engine; }
    nvinfer1::IExecutionContext&  context() { return *_context; }

    const char* inputName()  const { return _inputName.c_str(); }
    const char* outputName() const { return _outputName.c_str(); }

private:
    void buildFromOnnx(const std::string& onnxPath, const std::string& enginePath);
    void loadEngine(const std::string& enginePath);
    void resolveTensorNames();

    nvinfer1::IRuntime*           _runtime  = nullptr;
    nvinfer1::ICudaEngine*        _engine   = nullptr;
    nvinfer1::IExecutionContext*  _context  = nullptr;

    std::string _inputName;
    std::string _outputName;
};

/// Logger that forwards TensorRT messages to stderr.
class Logger : public nvinfer1::ILogger {
public:
    static Logger& instance();
    void log(Severity severity, const char* msg) noexcept override;
private:
    Logger() = default;
};
