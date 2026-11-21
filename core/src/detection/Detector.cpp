#include "plynth/core/detection/Detector.h"

#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace plynth {

namespace {

// The registry: detectors by id, guarded because registration and lookup may meet on different threads.
struct Registry {
    std::mutex mutex;
    std::map<std::string, std::unique_ptr<Detector>, std::less<>> detectors;
};

// The one registry. Made on first use so registrars in any file can use it while the program starts.
Registry& registry()
{
    static Registry instance;
    return instance;
}

// Says whether an id is a lower case name: a letter, then letters, digits, "_" or "-".
bool isWellFormedDetectorId(std::string_view id)
{
    if (id.empty() || !(id[0] >= 'a' && id[0] <= 'z')) return false;
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

// The ids of the registered detectors, comma separated, for messages.
std::string knownIds()
{
    std::string out;
    for (const Detector* d : allDetectors()) {
        if (!out.empty()) out += ", ";
        out += std::string(d->id());
    }
    return out.empty() ? "none" : out;
}

}

// Adds a detector unless its id is taken or badly formed.
bool registerDetector(std::unique_ptr<Detector> detector)
{
    if (!detector) return false;
    const std::string id(detector->id());
    if (!isWellFormedDetectorId(id)) return false;
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    return r.detectors.emplace(id, std::move(detector)).second;
}

DetectorRegistrar::DetectorRegistrar(std::unique_ptr<Detector> detector)
{
    registerDetector(std::move(detector));
}

// Looks a detector up by id.
const Detector* findDetector(std::string_view id)
{
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    const auto it = r.detectors.find(id);
    return it == r.detectors.end() ? nullptr : it->second.get();
}

// Lists the detectors in id order.
std::vector<const Detector*> allDetectors()
{
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    std::vector<const Detector*> out;
    out.reserve(r.detectors.size());
    for (const auto& entry : r.detectors) out.push_back(entry.second.get());
    return out;
}

// Runs a detector with checked inputs and checked output, then adds rooms and trims openings as asked.
DetectionResult runDetection(std::string_view detectorId, const GrayImage& image, double pixelsPerMetre,
                             const DetectionParams& params, std::stop_token stop)
{
    const auto started = std::chrono::steady_clock::now();
    DetectionResult result;
    result.detector = std::string(detectorId);
    result.params = params;
    result.pixelsPerMetre = pixelsPerMetre;
    result.imageWidth = image.width;
    result.imageHeight = image.height;

    const auto finish = [&](DetectionResult r) {
        r.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        return r;
    };
    const auto cancelled = [&]() {
        result.cancelled = true;
        result.points.clear();
        result.walls.clear();
        result.openings.clear();
        result.rooms.clear();
        return finish(std::move(result));
    };

    if (const Status s = checkDetectionParams(params); !s.ok()) {
        result.status = s;
        return finish(std::move(result));
    }
    if (!std::isfinite(pixelsPerMetre) || pixelsPerMetre <= 0.0) {
        result.status = fail(ErrorCode::InvalidValue, "The scale must be a number above zero.");
        return finish(std::move(result));
    }
    if (image.width <= 0 || image.height <= 0
        || image.pixels.size() != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height)) {
        result.status = fail(ErrorCode::ImageError, "The image is empty.");
        return finish(std::move(result));
    }
    const Detector* detector = findDetector(detectorId);
    if (!detector) {
        result.status = fail(ErrorCode::InvalidValue, "There is no detector " + std::string(detectorId)
                                                          + ". The detectors are: " + knownIds() + ".");
        return finish(std::move(result));
    }
    if (stop.stop_requested()) return cancelled();

    DetectionResult found = detector->detect(image, pixelsPerMetre, params, stop);
    if (found.cancelled || stop.stop_requested()) return cancelled();
    if (!found.status.ok()) {
        result.status = found.status;
        return finish(std::move(result));
    }
    result.points = std::move(found.points);
    result.walls = std::move(found.walls);
    result.openings = std::move(found.openings);
    if (const Status s = checkDetectionResult(result); !s.ok()) {
        result.status = fail(ErrorCode::InvalidValue, "The detector " + std::string(detectorId)
                                                          + " gave a result that breaks the rules: " + s.message);
        result.points.clear();
        result.walls.clear();
        result.openings.clear();
        return finish(std::move(result));
    }
    if (!params.openings) result.openings.clear();
    if (params.rooms) proposeRooms(result);
    if (stop.stop_requested()) return cancelled();
    return finish(std::move(result));
}

}
