#pragma once

#include "BuildGeometry.h"
#include "Dressing.h"
#include "FinishSettings.h"
#include "OpeningAssets.h"
#include "Plan.h"
#include "StairRailings.h"
#include "Trim.h"

#include <string>
#include <vector>

namespace plynth {

// The finishing parts of a build (DESIGN.md sections 7.13 steps 6, 9 and 10, 7.13a and 7.14): trim, catalog doors
// and windows, set dressing and lights. Kept beside FloorBuild (BuildGeometry.h), one FloorFinish per floor.

// Point light power in watts by kind, and their warm white colour.
inline constexpr double kCeilingLightPower = 60.0;
inline constexpr double kWallLightPower = 30.0;
inline constexpr double kStandingLightPower = 30.0;
inline constexpr Vec3 kLightColour{1.0, 0.9, 0.8};

// The power of a light of a kind.
double lightPower(LightKind kind);

// One light as the build makes it: Light_<floor>_<id> at location (world, the floor's level plus z).
struct LightBuild {
    Id id;
    LightKind kind = LightKind::Ceiling;
    Vec3 location;
    double power = 0.0;

    bool operator==(const LightBuild&) const = default;
};

// The lights of a floor at a level, in marker order.
std::vector<LightBuild> floorLights(const Floor& floor, double elevation);

// The catalog asset fitted into one opening.
struct OpeningAssetPlacement {
    Id opening;
    OpeningAssetBuild build;

    bool operator==(const OpeningAssetPlacement&) const = default;
};

// Everything finishing makes on one floor. trimMaterial is the trim's catalog id (empty: the neutral look) and
// trimSlot its slot.
struct FloorFinish {
    Id floor;
    std::vector<TrimBuild> trim;
    std::string trimMaterial;
    std::string trimSlot;
    std::vector<OpeningAssetPlacement> openingAssets;
    std::vector<DressingPlacement> dressing;
    std::vector<LightBuild> lights;
    // The railings of stairs on this floor and the guards round holes of stairs arriving here, with their
    // material and slot.
    std::vector<StairRailing> railings;
    std::string railingMaterial;
    std::string railingSlot;

    bool operator==(const FloorFinish&) const = default;
};

// The object name of a floor's trim of a kind: SM_<floor>_Skirting, _Cornice, _Casing or _Sill.
std::string trimObjectName(const Id& floor, TrimKind kind);

}
