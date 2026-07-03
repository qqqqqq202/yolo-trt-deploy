#include "visualizer.h"

#include <opencv2/imgproc.hpp>

Visualizer::Visualizer()
    : _personColor(0, 255, 0)       // green
    , _intruderColor(0, 0, 255)     // red
    , _zoneColor(255, 200, 0)       // cyan‑gold
    , _alertColor(0, 0, 255)        // red
    , _textThickness(2)
    , _textScale(0.7)
{}

void Visualizer::draw(cv::Mat&                    frame,
                      const std::vector<Detection>& detections,
                      const std::vector<Detection>& intrusions,
                      const IntrusionZone&          zone) {

    // ---- Draw intrusion zone ----
    cv::rectangle(frame, zone.rect(), _zoneColor, 2);
    cv::putText(frame, "ROI",
                cv::Point(zone.rect().x + 5, zone.rect().y + 20),
                cv::FONT_HERSHEY_SIMPLEX, 0.6, _zoneColor, 2);

    // ---- Draw all person detections ----
    for (const auto& d : detections) {
        bool isIntruder = false;
        for (const auto& intr : intrusions) {
            if (&d == &intr) { isIntruder = true; break; }
        }
        // Actually we can't compare pointers across copies; use a simple spatial check
        // We'll just draw all detections green, then intrusions red on top.
        cv::Rect r(static_cast<int>(d.box.x), static_cast<int>(d.box.y),
                   static_cast<int>(d.box.width), static_cast<int>(d.box.height));
        cv::rectangle(frame, r, _personColor, 2);

        std::string label = "person " + std::to_string(static_cast<int>(d.conf * 100)) + "%";
        cv::putText(frame, label, cv::Point(r.x, r.y - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, _personColor, 1);
    }

    // ---- Highlight intruders on top ----
    for (const auto& d : intrusions) {
        cv::Rect r(static_cast<int>(d.box.x), static_cast<int>(d.box.y),
                   static_cast<int>(d.box.width), static_cast<int>(d.box.height));
        cv::rectangle(frame, r, _intruderColor, 3);

        std::string label = "INTRUDER " + std::to_string(static_cast<int>(d.conf * 100)) + "%";
        cv::putText(frame, label, cv::Point(r.x, r.y - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, _intruderColor, 2);
    }

    // ---- Alert banner if any intruder ----
    if (!intrusions.empty()) {
        std::string alert = "ALERT: " + std::to_string(intrusions.size()) + " intruder(s)!";
        int baseline = 0;
        cv::Size textSize = cv::getTextSize(alert, cv::FONT_HERSHEY_SIMPLEX, 1.0, 2, &baseline);
        cv::Point topLeft((frame.cols - textSize.width) / 2, 40);

        // Background banner
        cv::rectangle(frame,
                      cv::Rect(topLeft.x - 10, topLeft.y - textSize.height - 10,
                               textSize.width + 20, textSize.height + 20),
                      cv::Scalar(0, 0, 255), cv::FILLED);
        cv::putText(frame, alert,
                    cv::Point(topLeft.x, topLeft.y),
                    cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255, 255, 255), 2);
    }

    // ---- Stats overlay ----
    cv::putText(frame,
                "Persons: " + std::to_string(detections.size()) +
                " | Intruders: " + std::to_string(intrusions.size()),
                cv::Point(10, frame.rows - 15),
                cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 255), 1);
}
