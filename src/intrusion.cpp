#include "intrusion.h"

IntrusionZone::IntrusionZone(const cv::Point2f& corner1, const cv::Point2f& corner2) {
    setRect(corner1, corner2);
}

void IntrusionZone::setRect(const cv::Point2f& corner1, const cv::Point2f& corner2) {
    float x = std::min(corner1.x, corner2.x);
    float y = std::min(corner1.y, corner2.y);
    float w = std::abs(corner2.x - corner1.x);
    float h = std::abs(corner2.y - corner1.y);
    _rect   = cv::Rect2f(x, y, w, h);
}

bool IntrusionZone::contains(const cv::Point2f& pt) const {
    return _rect.contains(pt);
}

std::vector<Detection>
IntrusionZone::checkIntrusions(const std::vector<Detection>& detections) const {
    std::vector<Detection> intruders;
    for (const auto& d : detections) {
        // Centre of the bounding box
        cv::Point2f centre(d.box.x + d.box.width  * 0.5f,
                           d.box.y + d.box.height * 0.5f);
        if (contains(centre)) {
            intruders.push_back(d);
        }
    }
    return intruders;
}
