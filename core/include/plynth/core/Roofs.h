#pragma once

#include "BuildGeometry.h"
#include "Plan.h"
#include "Status.h"
#include "Storeys.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// Roofs (DESIGN.md section 7.9). The roof is derived, not drawn: every piece of exposedArea (Storeys.h) is a
// roof section. A section takes the settings of the RoofSection entry of its floor whose anchor lies in it (the
// first in plan order when several do), or the plan's roof default. Roof types are registry entries: a
// built-in type is one file in core/src/roofs/types/ with a RoofTypeRegistrar at namespace scope. A type turns
// a section into finished faces in world coordinates; the Blender build only makes meshes from them.
//
// Every roof sits on the slab of its floor, at base = level(i + 1) (BuildGeometry.h floorElevations). The
// roof's eaves outline is the section offset outward by the overhang (mitred corners), minus the footprint of
// the floor above, so a roof never reaches into the storey beside it. Pitched roofs are kRoofThickness thick,
// measured straight down; their top surface passes through base at the section's outline where it is lowest.
//
// The built-in types (sort key in brackets), and the fields each uses:
//   flat     (10)  pitch no, direction no, overhang only without a parapet, parapet yes. A layer kRoofThickness
//                  thick over the eaves outline (the section itself when there is a parapet), and when parapet is
//                  above 0 a wall kParapetThickness thick along the inside of the section's outline, parapet high
//                  above the layer
//   gable    (20)  pitch, direction (the ridge), overhang. Two planes falling away from a ridge line along the
//                  direction through the middle of the section's extent across it; vertical gable fills between
//                  base and the roof's underside along the section's outline
//   hip      (30)  pitch, overhang. One plane per outline edge rising from it; a face is where its plane is the
//                  lowest. Needs a convex section outline (after dropping points in line within 1e-3 m); other
//                  sections fall back to gable
//   shed     (40)  pitch, direction (the way it falls), overhang. One plane; gable fills as for gable
//   sawtooth (50)  pitch, direction (the ridges), overhang. Bays kSawtoothBaySpan wide across the direction, each
//                  a plane rising across its bay, with a vertical glazed face at the high side of each bay
// Automatic direction: along the longer side of the section's bounding box on the plan (+x when it is square);
// a shed falls toward +y on the plan when the ridge would run along x, toward +x otherwise.

inline constexpr std::string_view kFlatRoofId = "flat";
inline constexpr std::string_view kGableRoofId = "gable";
inline constexpr std::string_view kHipRoofId = "hip";
inline constexpr std::string_view kShedRoofId = "shed";
inline constexpr std::string_view kSawtoothRoofId = "sawtooth";

inline constexpr double kMaxRoofPitch = 75.0;      // degrees
inline constexpr double kMinPitchedRoofPitch = 5.0; // degrees; a pitched type with less is refused by commands
inline constexpr double kMaxRoofOverhang = 2.0;    // metres
inline constexpr double kMaxParapet = 3.0;         // metres
inline constexpr double kRoofThickness = 0.15;     // metres
inline constexpr double kParapetThickness = 0.2;   // metres
inline constexpr double kSawtoothBaySpan = 3.0;    // metres

// One derived roof section.
struct RoofArea {
    Id floor;
    int index = 0;          // its place in exposedArea of its floor
    PlanArea area;          // plan coordinates
    Vec2 labelPoint;        // a position well inside it, for the label the plan draws and new anchors
    Id section;             // the RoofSection entry that applies; empty when the plan's default does
    RoofSettings settings;  // the settings that apply
    std::optional<std::string> material; // the entry's material; empty for the default or none set

    bool operator==(const RoofArea&) const = default;
};

// Every roof section of the plan, in floor order, then by index.
std::vector<RoofArea> roofAreas(const Plan& plan);

// The roof sections of one floor.
std::vector<RoofArea> roofAreasOf(const Plan& plan, std::string_view floorId);

// The roof section of a floor that holds a plan position. Empty when none does.
std::optional<RoofArea> roofAreaAt(const Plan& plan, std::string_view floorId, Vec2 position);

// RoofSection entries whose anchor lies in no section of their floor, or in a section another entry already
// governs. In plan order.
std::vector<Id> unusedRoofSections(const Plan& plan);

// What a roof type needs to know, in world coordinates.
struct RoofInput {
    WorldPolygon section;             // the section's area
    std::vector<WorldPolygon> eaves;  // the eaves outline as the header says
    double base = 0.0;                // world Z the roof sits on
    RoofSettings settings;
    Vec2 direction;                   // unit world direction resolved from settings.direction, never empty
};

class RoofType {
public:
    virtual ~RoofType() = default;

    // Stable id stored in plan.json, for example "gable". Lower case letters, digits and "_".
    virtual std::string_view id() const = 0;
    // Short name for the app, for example "Gable".
    virtual std::string_view name() const = 0;
    // Types are listed by this key, then by id.
    virtual int sortKey() const = 0;
    // Which settings the type uses; the app and the CLI list only those.
    virtual bool usesPitch() const = 0;
    virtual bool usesDirection() const = 0;
    virtual bool usesOverhang() const = 0;
    virtual bool usesParapet() const = 0;
    // Says whether the type can roof this section. When it cannot, the build uses fallback() and reports
    // roof_type_fallback with this status's message.
    virtual Status fits(const RoofInput& input) const = 0;
    // The type used when fits fails; "flat" for types that fit every section.
    virtual std::string_view fallback() const = 0;
    // The faces, only called when fits passes. Every face is planar, with its outline anticlockwise seen from
    // the side it faces.
    virtual std::vector<RoofFace> build(const RoofInput& input) const = 0;
};

// Adds a roof type to the registry. Returns false and drops it when its id is taken or not a well formed lower
// case name.
bool registerRoofType(std::unique_ptr<RoofType> type);

// Registers a roof type while the program starts, for example
// const RoofTypeRegistrar registrar(std::make_unique<GableRoof>());
struct RoofTypeRegistrar {
    explicit RoofTypeRegistrar(std::unique_ptr<RoofType> type);
};

// The roof type with this id. Null when there is none.
const RoofType* findRoofType(std::string_view id);

// Every registered roof type, sorted by sortKey and then id.
std::vector<const RoofType*> allRoofTypes();

// Checks roof settings a command would set: type registered, numbers in range, a pitched type (one that uses
// pitch) at kMinPitchedRoofPitch or more, direction from 0 up to 360 when set. InvalidValue with a plain
// sentence.
Status checkRoofSettings(const RoofSettings& settings);

// Something about a roof section the build reports. Codes and severities:
//   roof_type_unknown (warning: the type is not registered; the section gets a flat roof),
//   roof_type_fallback (warning: the type does not fit the section; it gets the type's fallback),
//   roof_section_unused (warning: an entry of unusedRoofSections; object is the entry id).
struct RoofProblem {
    Id floor;
    Id object; // the RoofSection entry id, or empty for a section on the default
    std::string code;
    std::string message; // one plain sentence

    bool operator==(const RoofProblem&) const = default;
};

// Works out the roofs of floor i at levels (floorElevations), with the problems found, in index order.
struct FloorRoofs {
    std::vector<RoofBuild> roofs;
    std::vector<RoofProblem> problems;
};

FloorRoofs floorRoofs(const Plan& plan, std::size_t floorIndex, const std::vector<double>& levels);

}
