#pragma once

#include "detector.h"

#include <opencv2/core.hpp>

/// Simple rectangular intrusion zone.
/// Triggers when a person's bounding‑box centre enters the rectangle.
class IntrusionZone {
public:
    /// Define the zone from two opposite corners (image‑pixel coordinates).
    IntrusionZone(const cv::Point2f& corner1, const cv::Point2f& corner2);

    /// Update the zone rectangle.
    void setRect(const cv::Point2f& corner1, const cv::Point2f& corner2);

    /// Check every person detection and return those that intrude.
    /// @param detections  All detections from the detector (pre‑filtered to person class)
    /// @return            Subset of detections whose centre lies inside the zone.
    std::vector<Detection> checkIntrusions(const std::vector<Detection>& detections) const;

    /// Is a single point inside the zone?
    bool contains(const cv::Point2f& pt) const;

    const cv::Rect2f& rect() const { return _rect; }

private:
    cv::Rect2f _rect;
};
