#pragma once

#include "BuildGeometry.h"
#include "BuildProblem.h"
#include "Collision.h"
#include "MaterialDefaults.h"
#include "Plan.h"
#include "Project.h"
#include "Trim.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// Build parts: the generic form of everything plan format 8 adds outside the walls, rooms, stairs and roofs
// (decks, railings, canopies, dormers, cladding, outside step cheeks, site, foundation, light strip housings).
// Core works out every face (DESIGN.md section 2: the geometry rules live only in core), and the Blender worker
// makes each part with one generic step, "parts", so a new kind of part needs no Blender code.
//
// Part makers are registry entries: one file each in core/src/parts/ with a PartMakerRegistrar at namespace
// scope. makeBuildPlan (BuildJob.h) runs every maker in order for every floor and once for the site, after the
// roofs, and puts what they give into BuildPlan (floorParts, siteParts, roofCuts, stripLights). Their problems go
// into BuildPlan::problems after a floor's roof problems.
//
// World coordinates as in BuildGeometry.h. Every part is one Blender object, named plainly (ExportJob.h "Name
// rules"), with custom properties plynth_kind and plynth_id, in its floor's collection (site parts in "Site").

// A solid raised straight up: outline and holes in world X and Y (outer ring anticlockwise, holes clockwise),
// from bottom to top in world Z. sideSlot, when not empty, is used on the side faces instead of slot.
struct PartPrism {
    std::vector<Vec2> outline;
    std::vector<std::vector<Vec2>> holes;
    double bottom = 0.0;
    double top = 0.0;
    std::string slot;
    std::string sideSlot;

    bool operator==(const PartPrism&) const = default;
};

// One planar face in world coordinates, anticlockwise seen from the side it faces, holes the other way.
struct PartFace {
    std::vector<Vec3> outline;
    std::vector<std::vector<Vec3>> holes;
    std::string slot;

    bool operator==(const PartFace&) const = default;
};

// A profile swept along a path (Trim.h Sweep), for rails, copings and gutters.
struct PartSweep {
    Sweep sweep;
    std::string slot;

    bool operator==(const PartSweep&) const = default;
};

// How the worker sets UVs on a part: World projects each face on the world plane its normal is closest to, in
// metres divided by the tile size (as the build's other faces); Along runs u along the part's longest sweep or
// prism side and v up.
enum class PartUv { World, Along };

// One object a maker gives.
struct BuildPart {
    std::string object;    // the object name, for example "f1_dk7k3q9xm2_Deck"
    std::string kind;      // plynth_kind, for example "deck"; a group rule takes it by this
    Id id;                 // plynth_id: the plan id of what it was made from
    std::vector<PartPrism> prisms;
    std::vector<PartFace> faces;
    std::vector<PartSweep> sweeps;
    bool bevel = true;     // the build's bevel on its edges (DESIGN.md section 7.13a)
    PartUv uv = PartUv::World;
    // Its collision (Collision.h): pieces raised from bottom to top, and convex hulls. Empty for none (glass,
    // light housings). Named <object>_Collision_<NN> in the build.
    std::vector<CollisionPiece> collision;
    std::vector<std::vector<Vec3>> hulls;

    bool operator==(const BuildPart&) const = default;
};

// A hole a maker cuts into a roof section's covering, underside and edge faces (a dormer), as a prism: every
// roof face of that floor's section index is cut where the prism passes through it.
struct RoofCut {
    Id floor;
    int index = 0; // RoofBuild::index
    PartPrism cutter;

    bool operator==(const RoofCut&) const = default;
};

// A strip of light: an area light along a straight piece of a light strip, facing one way (LightStrips.h).
struct StripLight {
    Id id;                   // the light strip
    Vec3 start;
    Vec3 end;
    Vec3 facing;             // unit
    double width = 0.02;     // metres across the strip
    double power = 0.0;      // watts for this piece: the strip's power per metre times its length
    Vec3 colour{1.0, 0.9, 0.8};

    bool operator==(const StripLight&) const = default;
};

// What a maker is given.
struct PartInput {
    const Project* project = nullptr;     // the resolved project
    const Floor* floor = nullptr;         // the floor being made; null for the site call
    std::size_t floorIndex = 0;
    const std::vector<double>* levels = nullptr; // floorElevations, ground floor at 0
    const std::vector<FloorBuild>* floors = nullptr; // everything made so far, roofs included
};

// What a maker gives for one call.
struct PartOutput {
    std::vector<BuildPart> parts;
    std::vector<RoofCut> roofCuts;
    std::vector<StripLight> lights;
    std::vector<BuildProblem> problems;
};

// The export group a maker's parts go to by default (ExportGroups.h): "structure" (walls, windows and doors) or
// "surfaces" (floors, roof and lights).
class PartMaker {
public:
    virtual ~PartMaker() = default;

    // Stable lower case id, for example "decks".
    virtual std::string_view id() const = 0;
    // Order among makers; lower first. Ties by id.
    virtual int order() const = 0;
    // The plynth_kind values its parts have.
    virtual std::vector<std::string> kinds() const = 0;
    // "structure" or "surfaces".
    virtual std::string_view exportGroup() const = 0;
    // The parts of one floor.
    virtual PartOutput floorParts(const PartInput& input) const = 0;
    // The parts on the site (floor is null). Empty by default.
    virtual PartOutput siteParts(const PartInput& input) const { (void)input; return {}; }
};

// Adds a maker. False, and it is dropped, when its id is taken or not a well formed lower case name.
bool registerPartMaker(std::unique_ptr<PartMaker> maker);

// Registers a maker while the program starts.
struct PartMakerRegistrar {
    explicit PartMakerRegistrar(std::unique_ptr<PartMaker> maker);
};

// Every maker, in order.
std::vector<const PartMaker*> allPartMakers();

// The slot of a part surface, by the rules of Materials.h and MaterialSlots.h: the plan's material when set,
// else the project default for surface. Parts share slots with every other built surface of the same surface:
// the slot's plain name is the surface's (MaterialSlots.h, "build parts"), for example Deck, Glass or
// Exterior_Wall, and when one plain name shows more than one material every use gets the material added
// (Deck_OakPlanks). That needs the whole build, so slot is a key the maker writes into a slot field as it is;
// makeBuildPlan checks its material with the library (missing_material, material_broken, object the part's id,
// or empty for a project default) and turns every key into the final slot name. A slot field may also hold a
// plain name with no material (for example "Light_Strip"): its neutral surface is the same name.
struct PartSlot {
    std::string slot;     // the key: "<plain name>|<neutral surface>|<material>"
    std::string material; // the material it names before the library check; empty for the neutral look
};
PartSlot partSlot(const Project& project, Surface surface, const std::optional<std::string>& material);

// The same for a slot that is no project surface, such as Soffit or Fascia: name is its plain name, look its
// neutral surface (BuildJob.h "surface" of a slot), material empty for the neutral look.
PartSlot partSlot(std::string_view name, std::string_view look, const std::optional<std::string>& material);

// The job form, written by makeBuildJob into each floor as "parts", "strip_lights", and once at the top level as
// "site_parts", each left out when empty (schema/blender-job-parts.schema.json):
//   part: {"object", "plynth_kind", "plynth_id", "bevel": true, "uv": "world" | "along",
//          "prisms": [{"outline": [[x, y], ...], "holes": [[[x, y], ...], ...], "bottom", "top", "slot",
//                      "side_slot": null or slot}],
//          "faces": [{"outline": [[x, y, z], ...], "holes": [[[x, y, z], ...], ...], "slot"}],
//          "sweeps": [{"closed", "sections": [[[x, y, z], ...], ...], "slot"}],
//          "collision": [{"outline": [[x, y], ...], "bottom", "top"}], "hulls": [[[x, y, z], ...], ...]}
//   strip light: {"id", "start": [x, y, z], "end": [x, y, z], "facing": [x, y, z], "width", "power",
//                 "colour": [r, g, b]}  (an area light, Light_<id>_<NN>, plynth_kind "light")
// Roof cuts are not in the job: core cuts the roof faces before it writes them. Site parts go into a collection
// named "Site"; a light the site call gives goes with the first floor.
Json buildPartToJson(const BuildPart& part);
Json stripLightToJson(const StripLight& light);

}
