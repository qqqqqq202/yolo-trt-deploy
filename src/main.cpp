#include "engine.h"
#include "detector.h"
#include "intrusion.h"
#include "visualizer.h"

#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

static void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --model <path>      ONNX model path (default: ../model/yolov8n.onnx)\n"
              << "  --engine <path>     TensorRT engine cache (default: auto from model)\n"
              << "  --video <path>      Video file (default: webcam 0)\n"
              << "  --output <path>     Save annotated result video (e.g. output.mp4)\n"
              << "  --no-display        Headless mode: don't open GUI window\n"
              << "  --conf <float>      Confidence threshold (default: 0.45)\n"
              << "  --nms <float>       NMS IoU threshold (default: 0.45)\n"
              << "  --roi <x1,y1,x2,y2> Intrusion zone rectangle (default: centre 30%)\n"
              << "  --help              Show this message\n"
              << "\nExamples:\n"
              << "  " << prog << " --video input.mp4 --output result.mp4\n"
              << "  " << prog << " --video input.mp4 --output result.mp4 --no-display\n"
              << "\nKeys:  q/ESC=quit  space=pause\n";
}

int main(int argc, char** argv) {
    // ---- Defaults ----
    std::string modelPath   = "../model/yolov8n.onnx";
    std::string enginePath;
    std::string videoPath;
    std::string outputPath;
    bool        noDisplay   = false;
    float       confThr     = 0.45f;
    float       nmsThr      = 0.45f;
    bool        roiSet      = false;
    cv::Point2f roiCorner1, roiCorner2;

    // ---- Parse CLI ----
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--model" && i + 1 < argc) {
            modelPath = argv[++i];
        } else if (arg == "--engine" && i + 1 < argc) {
            enginePath = argv[++i];
        } else if (arg == "--video" && i + 1 < argc) {
            videoPath = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            outputPath = argv[++i];
        } else if (arg == "--no-display") {
            noDisplay = true;
        } else if (arg == "--conf" && i + 1 < argc) {
            confThr = std::stof(argv[++i]);
        } else if (arg == "--nms" && i + 1 < argc) {
            nmsThr = std::stof(argv[++i]);
        } else if (arg == "--roi" && i + 1 < argc) {
            std::string roiStr = argv[++i];
            std::sscanf(roiStr.c_str(), "%f,%f,%f,%f",
                        &roiCorner1.x, &roiCorner1.y,
                        &roiCorner2.x, &roiCorner2.y);
            roiSet = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    std::cout << "========================================" << std::endl;
    std::cout << "  Intrusion Detector (YOLOv8 + TensorRT)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "[Config] model:  " << modelPath << std::endl;
    std::cout << "[Config] conf:   " << confThr << std::endl;
    std::cout << "[Config] nms:    " << nmsThr << std::endl;
    if (!outputPath.empty())
        std::cout << "[Config] output: " << outputPath << std::endl;

    // ---- Open video source ----
    cv::VideoCapture cap;
    if (videoPath.empty()) {
        std::cout << "[Video]  Opening webcam 0 ..." << std::endl;
        cap.open(0, cv::CAP_V4L2);
    } else {
        std::cout << "[Video]  Opening " << videoPath << " ..." << std::endl;
        cap.open(videoPath);
    }
    if (!cap.isOpened()) {
        std::cerr << "[ERROR] Cannot open video source" << std::endl;
        return 1;
    }

    int    frameW = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    int    frameH = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    double fps    = cap.get(cv::CAP_PROP_FPS);
    if (fps <= 0) fps = 30.0;  // fallback
    std::cout << "[Video]  Resolution: " << frameW << "x" << frameH
              << " @ " << fps << " fps" << std::endl;

    // ---- Video writer (if output specified) ----
    cv::VideoWriter writer;
    if (!outputPath.empty()) {
        int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
        writer.open(outputPath, fourcc, fps, cv::Size(frameW, frameH));
        if (!writer.isOpened()) {
            std::cerr << "[ERROR] Cannot open output video: " << outputPath << std::endl;
            return 1;
        }
        std::cout << "[Output] Writing to " << outputPath << std::endl;
    }

    // ---- Engine & Detector ----
    Engine   engine(modelPath, enginePath);
    Detector detector(engine, confThr, nmsThr);

    // ---- Intrusion zone ----
    if (!roiSet) {
        // Default: central rectangle ~30% of frame
        float cx1 = frameW * 0.35f, cy1 = frameH * 0.35f;
        float cx2 = frameW * 0.65f, cy2 = frameH * 0.65f;
        roiCorner1 = cv::Point2f(cx1, cy1);
        roiCorner2 = cv::Point2f(cx2, cy2);
    }
    IntrusionZone zone(roiCorner1, roiCorner2);
    std::cout << "[Zone]   ROI: (" << zone.rect().x << ", " << zone.rect().y
              << ") - (" << (zone.rect().x + zone.rect().width)
              << ", " << (zone.rect().y + zone.rect().height) << ")" << std::endl;

    Visualizer vis;

    // ---- GUI (unless --no-display) ----
    if (!noDisplay) {
        cv::namedWindow("Intrusion Detector", cv::WINDOW_NORMAL);
    }

    // ---- Main loop ----
    cv::Mat frame;
    int  frameCount = 0;
    bool paused     = false;
    auto tStart     = std::chrono::steady_clock::now();

    std::cout << "\n[Run] Processing... "
              << (noDisplay ? "(headless)" : "(q to quit)")
              << std::endl;

    while (true) {
        if (!paused) {
            cap >> frame;
            if (frame.empty()) {
                std::cout << "[Video] End of stream" << std::endl;
                break;
            }

            auto t0   = std::chrono::steady_clock::now();
            auto dets = detector.detect(frame);
            auto t1   = std::chrono::steady_clock::now();

            // Filter to person class only (COCO class 0)
            std::vector<Detection> persons;
            std::copy_if(dets.begin(), dets.end(), std::back_inserter(persons),
                         [](const Detection& d) { return d.klass == 0; });

            auto intruders = zone.checkIntrusions(persons);
            auto t2        = std::chrono::steady_clock::now();

            float inferMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
            float totalMs = std::chrono::duration<float, std::milli>(t2 - t0).count();

            // Draw everything
            vis.draw(frame, persons, intruders, zone);

            // FPS overlay
            char fpsText[64];
            std::snprintf(fpsText, sizeof(fpsText),
                          "FPS: %.1f | Infer: %.1f ms | Frame: %d",
                          1000.0f / totalMs, inferMs, frameCount);
            cv::putText(frame, fpsText, cv::Point(10, 25),
                        cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 255), 1);

            // Print intruder alerts to console
            if (!intruders.empty()) {
                std::cout << "[ALERT] Frame " << frameCount << ": "
                          << intruders.size() << " intruder(s) detected!" << std::endl;
            }

            ++frameCount;

            // Write to output video
            if (writer.isOpened()) {
                writer.write(frame);
            }
        }

        // Display (unless headless)
        if (!noDisplay) {
            cv::imshow("Intrusion Detector", frame);
            int key = cv::waitKey(1) & 0xFF;
            if (key == 'q' || key == 27) {       // q or ESC
                break;
            } else if (key == ' ') {              // space = pause
                paused = !paused;
                std::cout << "[Run] " << (paused ? "PAUSED" : "RESUMED") << std::endl;
            }
        }
    }

    // ---- Cleanup ----
    if (writer.isOpened()) {
        writer.release();
        std::cout << "[Output] Saved: " << outputPath << std::endl;
    }

    auto  tEnd   = std::chrono::steady_clock::now();
    float totalS = std::chrono::duration<float>(tEnd - tStart).count();
    std::cout << "\n[Summary] " << frameCount << " frames in "
              << totalS << " s  (" << (frameCount / totalS) << " FPS avg)" << std::endl;

    cv::destroyAllWindows();
    return 0;
}
