#pragma once

#include "Choices.h"
#include "Dressing.h"
#include "Project.h"
#include "Variation.h"

#include <vector>

namespace plynth {

// Resolving a build (milestone 15): the plan the user marked stays as it is, and the build works from a resolved
// copy with every choice filled in. The app, the CLI and the build use only this, so a preview of a seed and the
// build of it agree.
//
// In order:
// 1. variedPlan (Variation.h) with the project's variation settings.
// 2. Every door and window with autoFrame and no frame gets the catalog asset chooseOpeningAsset picks
//    (OpeningAssets.h) written into its frame, with autoFrame set to false, so the copy says exactly what is built.
// 3. Every room with dressing on gets its props (dressRoom on the varied floor).
// Resolving a resolved project with empty choices changes nothing.

// The props of one floor.
struct FloorDressing {
    Id floor;
    std::vector<DressingPlacement> props;

    bool operator==(const FloorDressing&) const = default;
};

// A frame the resolve picked.
struct PickedFrame {
    Id opening;
    std::string asset;

    bool operator==(const PickedFrame&) const = default;
};

struct ResolvedBuild {
    Project project;                      // the resolved copy
    std::vector<FloorDressing> dressing;  // one entry per floor, in plan order
    std::vector<VariationChange> changes; // what the seed changed
    std::vector<PickedFrame> frames;      // the catalog doors and windows that were picked, in plan order

    bool operator==(const ResolvedBuild&) const = default;
};

// Resolves a project with the library's choices.
ResolvedBuild resolveBuild(const Project& project, const BuildChoices& choices);

}
