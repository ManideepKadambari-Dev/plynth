#pragma once

#include "Detection.h"
#include "GrayImage.h"

#include <memory>
#include <stop_token>
#include <string_view>
#include <vector>

namespace plynth {

// The detector used when none is named.
inline constexpr std::string_view kDefaultDetectorId = "orthogonal";

// One way of finding walls in a plan image. Detectors are registry entries: each lives in its own file in
// core/src/detection/detectors/ and registers itself with a DetectorRegistrar, so adding one never means
// editing existing code. Detectors keep no state between calls, because the app runs them on worker threads
// and may run two at once.
class Detector {
public:
    virtual ~Detector() = default;

    // Stable lower case id, for example "orthogonal". Used by the CLI --detector option.
    virtual std::string_view id() const = 0;
    // Short name for the app, for example "Straight walls".
    virtual std::string_view name() const = 0;
    // One plain sentence on what it finds and what it leaves for the user to draw.
    virtual std::string_view description() const = 0;

    // Finds points, walls and openings in the image and fills only those lists, the image size and status.
    // The params are already checked. Checks stop at least between its steps and, when a stop was asked
    // for, returns at once with cancelled set and empty lists.
    virtual DetectionResult detect(const GrayImage& image, double pixelsPerMetre, const DetectionParams& params,
                                   std::stop_token stop) const = 0;
};

// Adds a detector to the registry. Returns false and drops it when its id is already taken or not a
// well formed lower case name.
bool registerDetector(std::unique_ptr<Detector> detector);

// Registers a detector while the program starts. A detector file holds one at namespace scope, for example
// const DetectorRegistrar registrar(std::make_unique<OrthogonalDetector>());
struct DetectorRegistrar {
    explicit DetectorRegistrar(std::unique_ptr<Detector> detector);
};

// The detector with this id. Null when there is none.
const Detector* findDetector(std::string_view id);

// Every registered detector, sorted by id.
std::vector<const Detector*> allDetectors();

// Runs one detection from start to end, and is what the app and the CLI call:
// 1. Checks the params (checkDetectionParams), pixelsPerMetre (finite, above zero) and the image (not empty).
// 2. Runs the detector, then checks its output with checkDetectionResult.
// 3. Drops the opening proposals when params.openings is false, and fills rooms with proposeRooms when
//    params.rooms is true.
// 4. Sets detector, params, pixelsPerMetre and elapsedMs.
// InvalidValue for an unknown detector (the message lists the known ids), bad params or scale, or a detector
// that broke the result rules. ImageError for an empty image. A stop asked for at any point gives an ok
// status with cancelled set. Safe to call from several threads at once. Never changes a plan: run
// markExisting on the result afterwards, on the thread that owns the plan.
DetectionResult runDetection(std::string_view detectorId, const GrayImage& image, double pixelsPerMetre,
                             const DetectionParams& params, std::stop_token stop = {});

}
