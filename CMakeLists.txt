cmake_minimum_required(VERSION 3.18)
project(IntrusionDetector VERSION 1.0 LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# ---- OpenCV ----
find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio highgui)

# ---- CUDA ----
# Use FindCUDAToolkit for modern CMake CUDA support
find_package(CUDAToolkit REQUIRED)

# ---- TensorRT ----
# After apt install, headers are in /usr/include/aarch64-linux-gnu (Jetson) or standard paths
find_path(TENSORRT_INCLUDE_DIR NvInfer.h
    HINTS /usr/include /usr/include/x86_64-linux-gnu /usr/local/tensorrt/include
    PATH_SUFFIXES tensorrt
)
find_library(TENSORRT_LIBRARY nvinfer
    HINTS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/tensorrt/lib
)
find_library(TENSORRT_ONNX_PARSER nvonnxparser
    HINTS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/tensorrt/lib
)
find_library(TENSORRT_PLUGIN nvinfer_plugin
    HINTS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/tensorrt/lib
)

message(STATUS "TensorRT include: ${TENSORRT_INCLUDE_DIR}")
message(STATUS "TensorRT lib:     ${TENSORRT_LIBRARY}")
message(STATUS "OpenCV libs:      ${OpenCV_LIBS}")

# ---- Sources ----
set(SOURCES
    src/main.cpp
    src/engine.cpp
    src/detector.cpp
    src/intrusion.cpp
    src/visualizer.cpp
)

set(HEADERS
    include/engine.h
    include/detector.h
    include/intrusion.h
    include/visualizer.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${TENSORRT_INCLUDE_DIR}
    ${OpenCV_INCLUDE_DIRS}
    ${CUDAToolkit_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${TENSORRT_LIBRARY}
    ${TENSORRT_ONNX_PARSER}
    ${TENSORRT_PLUGIN}
    ${OpenCV_LIBS}
    CUDA::cudart
)
