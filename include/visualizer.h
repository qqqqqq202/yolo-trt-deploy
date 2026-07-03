#pragma once

#include "detector.h"
#include "intrusion.h"

#include <opencv2/core.hpp>

/// Draw detection boxes, the intrusion zone, and alert text onto a frame.
class Visualizer {
public:
    Visualizer();

    /// Draw everything for one frame.
    /// @param frame       Input/output BGR image.
    /// @param detections  All person detections.
    /// @param intrusions  Subset that triggered intrusion.
    /// @param zone        The intrusion zone (drawn as a green rectangle).
    void draw(cv::Mat&                    frame,
              const std::vector<Detection>& detections,
              const std::vector<Detection>& intrusions,
              const IntrusionZone&          zone);

private:
    cv::Scalar _personColor;      // normal person bbox
    cv::Scalar _intruderColor;    // intruder bbox
    cv::Scalar _zoneColor;        // ROI rectangle
    cv::Scalar _alertColor;       // alert text
    int        _textThickness;
    double     _textScale;
};
