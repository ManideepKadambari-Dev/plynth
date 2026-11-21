#include "FakeDetector.h"

#include <plynth/core/detection/Detector.h>

#include <memory>

namespace plynth::test {

namespace {

// Returns the fixed rectangle, or a cancelled result when a stop was asked for.
class FakeDetector : public Detector {
public:
    std::string_view id() const override { return kFakeDetectorId; }
    std::string_view name() const override { return "Fixed rectangle"; }
    std::string_view description() const override { return "Gives the same rectangle every time, for tests."; }

    DetectionResult detect(const GrayImage& image, double, const DetectionParams&, std::stop_token stop) const override
    {
        DetectionResult r;
        if (stop.stop_requested()) {
            r.cancelled = true;
            return r;
        }
        r = fixedProposals();
        r.imageWidth = image.width;
        r.imageHeight = image.height;
        return r;
    }
};

// A second detector, so the registry has more than one entry to sort.
class SecondDetector : public FakeDetector {
public:
    std::string_view id() const override { return kFakeSecondDetectorId; }
};

// A detector that breaks the result rules: two walls that cross without a shared point.
class BrokenDetector : public FakeDetector {
public:
    std::string_view id() const override { return kFakeBrokenDetectorId; }

    DetectionResult detect(const GrayImage&, double, const DetectionParams&, std::stop_token) const override
    {
        DetectionResult r;
        r.points = {{0, 0}, {2, 2}, {0, 2}, {2, 0}};
        r.walls = {{0, 1, 0.2, ProposalState::New}, {2, 3, 0.2, ProposalState::New}};
        return r;
    }
};

const DetectorRegistrar fixedRegistrar(std::make_unique<FakeDetector>());
const DetectorRegistrar secondRegistrar(std::make_unique<SecondDetector>());
const DetectorRegistrar brokenRegistrar(std::make_unique<BrokenDetector>());

}

}
