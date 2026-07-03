#include "engine.h"

#include <NvOnnxParser.h>

#include <fstream>
#include <iostream>
#include <vector>

// -----------------------------------------------------------------------------
// Logger
// -----------------------------------------------------------------------------

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::log(Severity severity, const char* msg) noexcept {
    if (severity <= Severity::kWARNING) {
        std::cerr << "[TensorRT] " << msg << std::endl;
    }
}

// -----------------------------------------------------------------------------
// Engine
// -----------------------------------------------------------------------------

Engine::Engine(const std::string& onnxPath, std::string enginePath) {
    if (enginePath.empty()) {
        enginePath = onnxPath.substr(0, onnxPath.rfind('.')) + ".engine";
    }

    // Check cached engine first
    std::ifstream f(enginePath, std::ios::binary);
    if (f.good()) {
        f.close();
        std::cout << "[Engine] Loading cached engine: " << enginePath << std::endl;
        loadEngine(enginePath);
    } else {
        std::cout << "[Engine] Building engine from ONNX: " << onnxPath << std::endl;
        buildFromOnnx(onnxPath, enginePath);
    }

    _context = _engine->createExecutionContext();
    if (!_context) {
        throw std::runtime_error("Failed to create execution context");
    }

    resolveTensorNames();
}

Engine::~Engine() {
    delete _context;
    delete _engine;
    delete _runtime;
}

void Engine::resolveTensorNames() {
    int nbIO = _engine->getNbIOTensors();
    for (int i = 0; i < nbIO; ++i) {
        const char* name = _engine->getIOTensorName(i);
        auto mode = _engine->getTensorIOMode(name);
        if (mode == nvinfer1::TensorIOMode::kINPUT) {
            _inputName = name;
        } else if (mode == nvinfer1::TensorIOMode::kOUTPUT) {
            _outputName = name;
        }
    }
    std::cout << "[Engine] Input:  \"" << _inputName << "\"" << std::endl;
    std::cout << "[Engine] Output: \"" << _outputName << "\"" << std::endl;
}

void Engine::buildFromOnnx(const std::string& onnxPath, const std::string& enginePath) {
    auto builder = std::unique_ptr<nvinfer1::IBuilder>(
        nvinfer1::createInferBuilder(Logger::instance()));
    if (!builder) throw std::runtime_error("createInferBuilder failed");

    // TRT 11: networks are always explicit‑batch; pass 0
    auto network = std::unique_ptr<nvinfer1::INetworkDefinition>(
        builder->createNetworkV2(0));
    if (!network) throw std::runtime_error("createNetworkV2 failed");

    // ONNX parser
    auto parser = std::unique_ptr<nvonnxparser::IParser>(
        nvonnxparser::createParser(*network, Logger::instance()));
    if (!parser) throw std::runtime_error("createParser failed");

    if (!parser->parseFromFile(onnxPath.c_str(),
                               static_cast<int>(nvinfer1::ILogger::Severity::kWARNING))) {
        throw std::runtime_error("Failed to parse ONNX: " + onnxPath);
    }

    auto config = std::unique_ptr<nvinfer1::IBuilderConfig>(builder->createBuilderConfig());
    config->setMemoryPoolLimit(nvinfer1::MemoryPoolType::kWORKSPACE, 1ULL << 30); // 1 GiB

    // TRT 11: FP16 is handled automatically by the builder when beneficial.
    // TF32 is enabled by default; we keep it for better throughput.

    auto plan = std::unique_ptr<nvinfer1::IHostMemory>(
        builder->buildSerializedNetwork(*network, *config));
    if (!plan) throw std::runtime_error("buildSerializedNetwork failed");

    // Serialize to disk
    {
        std::ofstream out(enginePath, std::ios::binary);
        if (!out) throw std::runtime_error("Cannot write engine to: " + enginePath);
        out.write(static_cast<const char*>(plan->data()), plan->size());
        std::cout << "[Engine] Saved: " << enginePath
                  << " (" << plan->size() / 1024 << " KiB)" << std::endl;
    }

    _runtime = nvinfer1::createInferRuntime(Logger::instance());
    _engine  = _runtime->deserializeCudaEngine(plan->data(), plan->size());
    if (!_engine) throw std::runtime_error("deserializeCudaEngine failed");
}

void Engine::loadEngine(const std::string& enginePath) {
    std::ifstream in(enginePath, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("Cannot open engine file: " + enginePath);

    size_t size = in.tellg();
    in.seekg(0);

    std::vector<char> data(size);
    in.read(data.data(), size);
    if (in.fail()) throw std::runtime_error("Failed to read engine file");

    _runtime = nvinfer1::createInferRuntime(Logger::instance());
    _engine  = _runtime->deserializeCudaEngine(data.data(), size);
    if (!_engine) throw std::runtime_error("deserializeCudaEngine failed");

    std::cout << "[Engine] Loaded: " << enginePath
              << " (" << size / 1024 << " KiB)" << std::endl;
}
