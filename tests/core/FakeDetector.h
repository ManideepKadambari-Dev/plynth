#pragma once

#include <plynth/core/detection/Detection.h>

#include <string_view>

namespace plynth::test {

// Ids of the detectors registered by FakeDetector.cpp.
inline constexpr std::string_view kFakeDetectorId = "test-fixed";
inline constexpr std::string_view kFakeSecondDetectorId = "test-aaa-second";
inline constexpr std::string_view kFakeBrokenDetectorId = "test-broken";

// What the fake detector returns: a 6 by 4 m rectangle of 0.3 m walls split by a wall at x 3, with one door
// on the first wall. Points 0 to 5 are (0,0) (3,0) (6,0) (6,4) (3,4) (0,4). Walls 0 to 5 run round the
// outside and wall 6 is the one across, from point 1 to point 4. Rooms are not filled in.
inline DetectionResult fixedProposals()
{
    DetectionResult r;
    r.detector = std::string(kFakeDetectorId);
    r.pixelsPerMetre = 100.0;
    r.points = {{0, 0}, {3, 0}, {6, 0}, {6, 4}, {3, 4}, {0, 4}};
    const std::size_t ends[7][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 0}, {1, 4}};
    for (const auto& e : ends) {
        ProposedWall w;
        w.from = e[0];
        w.to = e[1];
        w.thickness = 0.3;
        r.walls.push_back(w);
    }
    ProposedOpening o;
    o.wall = 0;
    o.type = OpeningType::Door;
    o.offset = 1.5;
    o.width = 0.9;
    r.openings.push_back(o);
    return r;
}

}
