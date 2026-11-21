#include <plynth/core/detection/Detector.h>
#include <plynth/core/detection/GrayImage.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace plynth;

// Hidden from the normal run. Run it on a Release build with "[.perf]".
TEST_CASE("detection time on the 2400 by 1600 sample", "[.perf]") {
    const auto start = std::chrono::steady_clock::now();
    const GrayImageLoad load = loadGrayImage(std::filesystem::path(PLYNTH_SAMPLES_DIR) / "plans" / "apartment-01.png");
    const double loadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    REQUIRE(load.status.ok());

    std::vector<double> times;
    for (int i = 0; i < 5; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        const DetectionResult r = runDetection("orthogonal", *load.image, 100.0, {});
        times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        REQUIRE(r.status.ok());
        REQUIRE(r.walls.size() == 16);
    }
    std::sort(times.begin(), times.end());
    const double median = times[2];
    std::cout << "load " << loadMs << " ms, detection median " << median << " ms (min " << times.front() << ", max "
              << times.back() << ")\n";
#ifdef NDEBUG
    REQUIRE(median < 300.0);
    REQUIRE(loadMs < 150.0);
#endif
}
