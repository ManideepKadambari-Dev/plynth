#include "plynth/core/detection/Detector.h"

#include "OrthogonalDetection.h"

#include <memory>
#include <utility>

namespace plynth {

namespace {

// Finds straight walls that run horizontally or vertically, so slightly skewed scans work too.
class OrthogonalDetector : public Detector {
public:
    std::string_view id() const override { return "orthogonal"; }
    std::string_view name() const override { return "Straight walls"; }
    std::string_view description() const override
    {
        return "Finds horizontal and vertical walls. Diagonal and curved walls are left for you to draw.";
    }

    DetectionResult detect(const GrayImage& image, double pixelsPerMetre, const DetectionParams& params,
                           std::stop_token stop) const override
    {
        return detectOrthogonal(image, pixelsPerMetre, params, std::move(stop));
    }
};

const DetectorRegistrar registrar(std::make_unique<OrthogonalDetector>());

}

}
