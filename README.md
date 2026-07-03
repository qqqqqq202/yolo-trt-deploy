# Intrusion Detector — YOLOv8 + TensorRT

基于 YOLOv8 的人员闯入检测系统，C++ TensorRT 高性能推理。

## 架构

```
video/camera → [OpenCV] → [TensorRT YOLOv8] → [NMS] → [ROI Intrusion] → [Display]
```

## 环境要求

| 组件 | 版本 |
|------|------|
| Ubuntu | 24.04 |
| CUDA | ≥12.x |
| TensorRT | ≥10.x |
| OpenCV | ≥4.6 |
| CMake | ≥3.18 |
| Python (仅导出) | 3.10 + ultralytics |

## 快速开始

### 1. 导出 ONNX 模型

```bash
conda activate trt-export
python scripts/export_onnx.py --model yolov8n
# 生成 model/yolov8n.onnx
```

### 2. 编译

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 3. 运行

```bash
# 使用默认摄像头
./IntrusionDetector --model ../model/yolov8n.onnx

# 使用视频文件
./IntrusionDetector --model ../model/yolov8n.onnx --video /path/to/video.mp4

# 自定义 ROI
./IntrusionDetector --model ../model/yolov8n.onnx --roi 200,150,500,400
```

## 命令行参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--model` | `../model/yolov8n.onnx` | ONNX 模型路径 |
| `--engine` | 自动生成 | TensorRT engine 缓存路径 |
| `--video` | 摄像头 0 | 视频文件路径 |
| `--conf` | 0.45 | 置信度阈值 |
| `--nms` | 0.45 | NMS IoU 阈值 |
| `--roi` | 画面中央 30% | 闯入区域 `x1,y1,x2,y2` |

## 操作按键

| 按键 | 功能 |
|------|------|
| `q` / `ESC` | 退出 |
| `Space` | 暂停/恢复 |

## 工作原理

1. **首次运行**：从 ONNX 构建 TensorRT engine 并序列化到 `.engine` 文件
2. **后续运行**：直接加载缓存的 `.engine` 文件，启动更快
3. **推理**：640×640 输入，FP16 推理，NMS 后处理
4. **闯入判定**：检测所有人的 bbox，计算中心点是否落入 ROI 矩形
5. **告警**：画面顶部红色横幅 + intruder 红色框 + 绿色框为普通人员
