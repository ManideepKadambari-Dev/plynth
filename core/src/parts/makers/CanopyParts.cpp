#include "plynth/core/BuildParts.h"
#include "plynth/core/Canopies.h"
#include "plynth/core/Collision.h"
#include "plynth/core/MaterialSlots.h"
#include "plynth/core/Roofs.h"
#include "plynth/core/Storeys.h"

#include "PlanRingClean.h"
#include "parts/PartGeometry.h"
#include "roofs/RoofParts.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace plynth {

namespace {

namespace pg = partgeometry;

// Heights nearer than this touch without overlapping, in metres.
constexpr double kLevelSlack = 1e-6;
// Less than this many square metres left of a canopy is nothing.
constexpr double kNothingLeft = 1e-4;

// A storey as a canopy meets it: its footprint and the heights it fills, from the underside of the slab it
// stands on (its own level for the canopy's floor) to the next floor's level.
struct Storey {
    std::vector<WorldPolygon> footprint;
    double low = 0.0;
    double high = 0.0;
};

// A roof section of the canopy's floor: its eaves as seen from above and the heights it fills.
struct RoofReach {
    std::vector<WorldPolygon> eaves;
    double low = 0.0;
    double high = 0.0;
};

// The slot of each kind of canopy face.
struct CanopySlots {
    std::string covering;
    std::string soffit;
    std::string fascia;
    std::string wall;
    std::string glass;

    // The slot of a face by what it is.
    const std::string& of(RoofPart part) const
    {
        switch (part) {
        case RoofPart::Covering: return covering;
        case RoofPart::Underside: return soffit;
        case RoofPart::Edge: return fascia;
        case RoofPart::Glazing: return glass;
        case RoofPart::Gable:
        case RoofPart::Parapet: return wall;
        }
        return wall;
    }
};

// What one canopy is built as.
struct CanopyShape {
    std::vector<RoofFace> faces;
    std::vector<WorldPolygon> slab; // the slab from the underside up to the roof's base
    std::string type;               // the roof type built
    bool plain = false;             // a plain flat slab with no roof of its own on top
    double base = 0.0;
    double top = 0.0;               // the highest point
    std::string fallback;           // why the chosen type was not built, when it was not
};

// Says whether two height ranges overlap by more than a touch.
bool overlaps(double lowA, double highA, double lowB, double highB)
{
    return lowA < highB - kLevelSlack && lowB < highA - kLevelSlack;
}

// The storeys a canopy of floor i may meet: that floor and every floor above it.
std::vector<Storey> storeysFrom(const Plan& plan, std::size_t floorIndex, const std::vector<double>& levels)
{
    std::vector<Storey> out;
    for (std::size_t j = floorIndex; j < plan.floors.size() && j < levels.size(); ++j) {
        Storey storey;
        storey.footprint = pg::worldPolygonsOf(floorFootprint(plan.floors[j]));
        storey.low = j == floorIndex ? levels[j] : levels[j] - plan.floors[j - 1].slabThickness;
        storey.high = levels[j] + floorRise(plan.floors[j]);
        if (!storey.footprint.empty()) out.push_back(std::move(storey));
    }
    return out;
}

// The roof sections of the canopy's floor, with their eaves from the covering faces seen from above.
std::vector<RoofReach> roofReaches(const FloorBuild* build)
{
    std::vector<RoofReach> out;
    if (!build) return out;
    for (const RoofBuild& roof : build->roofs) {
        RoofReach reach;
        reach.low = 1e300;
        reach.high = -1e300;
        Clipper2Lib::PathsD covering;
        for (const RoofFace& face : roof.faces) {
            for (const Vec3& p : face.outline) {
                reach.low = std::min(reach.low, p.z);
                reach.high = std::max(reach.high, p.z);
            }
            if (face.part != RoofPart::Covering) continue;
            std::vector<Vec2> outline;
            for (const Vec3& p : face.outline) outline.push_back({p.x, p.y});
            covering.push_back(pg::pathOf(outline));
            for (const std::vector<Vec3>& hole : face.holes) {
                std::vector<Vec2> ring;
                for (const Vec3& p : hole) ring.push_back({p.x, p.y});
                covering.push_back(pg::pathOf(ring));
            }
        }
        if (covering.empty()) continue;
        reach.eaves = pg::polygonsOf(clipped(Clipper2Lib::ClipType::Union, covering, {}));
        if (!reach.eaves.empty()) out.push_back(std::move(reach));
    }
    return out;
}

// Everything that stops a canopy filling the heights from low to high: the storeys there and, on its own
// floor, the eaves of the roof sections there, as one set.
std::vector<WorldPolygon> blockedBetween(const std::vector<Storey>& storeys, const std::vector<RoofReach>& roofs,
                                         double low, double high)
{
    std::vector<WorldPolygon> all;
    for (const Storey& storey : storeys) {
        if (overlaps(low, high, storey.low, storey.high)) all.insert(all.end(), storey.footprint.begin(), storey.footprint.end());
    }
    for (const RoofReach& roof : roofs) {
        if (overlaps(low, high, roof.low, roof.high)) all.insert(all.end(), roof.eaves.begin(), roof.eaves.end());
    }
    if (all.empty()) return all;
    return roofparts::joined(all, {});
}

// The unit world direction a roof type gets: the setting, or along the longer side of the section's bounding box.
Vec2 directionOf(const RoofSettings& settings, const WorldPolygon& section)
{
    if (settings.direction) {
        const double r = *settings.direction * std::numbers::pi / 180.0;
        return {std::cos(r), -std::sin(r)};
    }
    double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
    for (const Vec2& p : section.outline) {
        x0 = std::min(x0, p.x);
        x1 = std::max(x1, p.x);
        y0 = std::min(y0, p.y);
        y1 = std::max(y1, p.y);
    }
    return (x1 - x0) >= (y1 - y0) ? Vec2{1.0, 0.0} : Vec2{0.0, -1.0};
}

// The eaves of sections as floorRoofs makes them: grown by the overhang when the type uses one, less what blocks.
std::vector<WorldPolygon> eavesOf(const std::vector<WorldPolygon>& sections, const RoofType& type,
                                  const RoofSettings& settings, const std::vector<WorldPolygon>& blocked)
{
    std::vector<WorldPolygon> eaves = sections;
    bool moved = false;
    if (type.usesOverhang() && settings.overhang > 1e-9) {
        eaves = roofparts::inset(eaves, -settings.overhang);
        moved = true;
    }
    if (!blocked.empty()) {
        eaves = roofparts::without(eaves, blocked);
        moved = true;
    }
    return moved ? roofparts::tidied(eaves) : eaves;
}

// What a roof type is given for one piece of a canopy.
RoofInput inputOf(const WorldPolygon& section, const RoofType& type, const RoofSettings& settings, double base,
                  const std::vector<WorldPolygon>& blocked)
{
    RoofInput input;
    input.section = section;
    input.settings = settings;
    input.base = base;
    input.direction = directionOf(settings, section);
    input.above = blocked;
    input.eaves = eavesOf({section}, type, settings, blocked);
    return input;
}

// Says whether a vertical face stands wholly along what blocks the canopy, where the building's wall closes it.
bool standsAgainst(const RoofFace& face, const std::vector<WorldPolygon>& blocked)
{
    if (blocked.empty() || face.outline.size() < 2) return false;
    Vec2 a{face.outline[0].x, face.outline[0].y};
    Vec2 b = a;
    double longest = -1.0;
    for (const Vec3& p : face.outline) {
        for (const Vec3& q : face.outline) {
            const double d = std::hypot(q.x - p.x, q.y - p.y);
            if (d > longest) {
                longest = d;
                a = {p.x, p.y};
                b = {q.x, q.y};
            }
        }
    }
    if (longest < 1e-9) return false;
    return roofparts::freeParts(a, b, blocked).empty();
}

// The faces and slab of a canopy given what blocks it. No faces when nothing is left of it.
CanopyShape shapeOf(const Canopy& canopy, const RoofType& chosen, const WorldPolygon& outline, double bottom,
                    const std::vector<WorldPolygon>& blocked)
{
    CanopyShape shape;
    const std::vector<WorldPolygon> sections = roofparts::tidied(roofparts::without({outline}, blocked));
    if (pg::areaOf(sections) < kNothingLeft) return shape;

    const RoofType* type = &chosen;
    const double firstBase = bottom + canopy.thickness;
    for (const WorldPolygon& section : sections) {
        const Status fits = type->fits(inputOf(section, *type, canopy.roof, firstBase, blocked));
        if (fits.ok()) continue;
        shape.fallback = fits.message;
        const RoofType* other = findRoofType(type->fallback());
        type = other ? other : findRoofType(kFlatRoofId);
        break;
    }
    if (!type) return shape;
    shape.type = std::string(type->id());
    shape.plain = type->id() == kFlatRoofId && canopy.roof.parapet <= 0.0;
    // A pitched roof is kRoofThickness thick below its top, which meets the base at the outline, so it sits at
    // least that far above the underside and never shows below the soffit.
    shape.base = bottom + (type->usesPitch() ? std::max(canopy.thickness, kRoofThickness) : canopy.thickness);

    const auto add = [&shape](std::vector<RoofFace> more) {
        for (RoofFace& face : more) {
            for (RoofFace& piece : tidyRoofFace(face)) shape.faces.push_back(std::move(piece));
        }
    };
    if (shape.plain) {
        shape.slab = eavesOf(sections, *type, canopy.roof, blocked);
        add(roofparts::flatFaces(shape.slab, shape.base, true, RoofPart::Covering));
    } else {
        shape.slab = sections;
        for (const WorldPolygon& section : sections) {
            std::vector<RoofFace> faces = type->build(inputOf(section, *type, canopy.roof, shape.base, blocked));
            std::erase_if(faces, [&](const RoofFace& f) { return f.part == RoofPart::Gable && standsAgainst(f, blocked); });
            add(std::move(faces));
        }
    }
    add(roofparts::flatFaces(shape.slab, bottom, false, RoofPart::Underside));
    add(roofparts::ringWalls(shape.slab, bottom, shape.base, RoofPart::Edge, blocked));

    shape.top = shape.base;
    for (const RoofFace& face : shape.faces) {
        for (const Vec3& p : face.outline) shape.top = std::max(shape.top, p.z);
    }
    return shape;
}

// Says whether something under the canopy, within kCanopySupportReach of its outline, holds it up: a post
// (canopyPosts), a wall of the floor or a deck.
bool supported(const Floor& floor, const Canopy& canopy, const std::vector<Vec2>& planOutline)
{
    if (!canopyPosts(floor, canopy).empty()) return true;
    const Clipper2Lib::PathsD reach =
        offsetByTurning(Clipper2Lib::PathsD{pg::pathOf(pg::anticlockwise(planOutline))}, kCanopySupportReach, 5.0);
    if (reach.empty()) return false;
    Clipper2Lib::PathsD held;
    for (const PlanArea& area : floorFootprint(floor)) {
        held.push_back(pg::pathOf(area.outline));
        for (const std::vector<Vec2>& hole : area.holes) held.push_back(pg::pathOf(hole));
    }
    for (const Deck& deck : floor.decks) held.push_back(pg::pathOf(pg::anticlockwise(pg::pointsOf(deck.outline))));
    if (held.empty()) return false;
    const Clipper2Lib::PathsD under = clipped(Clipper2Lib::ClipType::Intersection, reach,
                                              clipped(Clipper2Lib::ClipType::Union, held, {}));
    return pg::areaOf(under) > pg::kTinyPartArea;
}

// The height of a face's plane at a point seen from above. Empty for a face that stands upright.
std::optional<double> heightOn(const RoofFace& face, Vec2 p)
{
    if (face.outline.size() < 3) return std::nullopt;
    double nx = 0.0;
    double ny = 0.0;
    double nz = 0.0;
    for (std::size_t i = 0; i < face.outline.size(); ++i) {
        const Vec3& a = face.outline[i];
        const Vec3& b = face.outline[(i + 1) % face.outline.size()];
        nx += (a.y - b.y) * (a.z + b.z);
        ny += (a.z - b.z) * (a.x + b.x);
        nz += (a.x - b.x) * (a.y + b.y);
    }
    if (std::fabs(nz) < 1e-9 * std::sqrt(nx * nx + ny * ny + nz * nz) || std::fabs(nz) < 1e-15) return std::nullopt;
    const Vec3& o = face.outline.front();
    return o.z - (nx * (p.x - o.x) + ny * (p.y - o.y)) / nz;
}

// The collision of a canopy: its slab as convex pieces, and a hull for every convex piece of a roof covering.
void collisionOf(BuildPart& part, const CanopyShape& shape, double bottom)
{
    for (const WorldPolygon& polygon : shape.slab) {
        for (std::vector<Vec2>& piece : convexPieces(polygon)) part.collision.push_back({std::move(piece), bottom, shape.base});
    }
    if (shape.plain) return;
    for (const RoofFace& face : shape.faces) {
        if (face.part != RoofPart::Covering) continue;
        WorldPolygon seen;
        for (const Vec3& p : face.outline) seen.outline.push_back({p.x, p.y});
        if (ringAreaOf(seen.outline) <= 0.0) continue;
        for (const std::vector<Vec3>& hole : face.holes) {
            std::vector<Vec2> ring;
            for (const Vec3& p : hole) ring.push_back({p.x, p.y});
            seen.holes.push_back(std::move(ring));
        }
        for (const std::vector<Vec2>& piece : convexPieces(seen)) {
            std::vector<Vec3> hull;
            bool flatOut = false;
            for (const Vec2& p : piece) {
                const std::optional<double> z = heightOn(face, p);
                if (!z) flatOut = true;
                hull.push_back({p.x, p.y, z.value_or(0.0)});
            }
            if (flatOut) continue;
            for (const Vec2& p : piece) hull.push_back({p.x, p.y, *heightOn(face, p) - kRoofThickness});
            part.hulls.push_back(std::move(hull));
        }
    }
}

// The canopies of a floor (Canopies.h).
class CanopyMaker final : public PartMaker {
public:
    std::string_view id() const override { return "canopies"; }
    int order() const override { return 40; }
    std::vector<std::string> kinds() const override { return {"canopy"}; }
    std::string_view exportGroup() const override { return "surfaces"; }

    // Every canopy of the floor as one part each, with its problems.
    PartOutput floorParts(const PartInput& input) const override
    {
        PartOutput out;
        if (!input.project || !input.floor || !input.levels || !input.floors) return out;
        if (input.floor->canopies.empty() || input.floorIndex >= input.levels->size()) return out;
        const Project& project = *input.project;
        const Floor& floor = *input.floor;
        const double level = (*input.levels)[input.floorIndex];
        const FloorBuild* build = input.floors->empty() ? nullptr : &input.floors->back();
        if (build && build->id != floor.id) build = nullptr;
        const std::vector<Storey> storeys = storeysFrom(project.plan, input.floorIndex, *input.levels);
        const std::vector<RoofReach> roofs = roofReaches(build);

        for (const Canopy& canopy : floor.canopies) {
            const auto problem = [&](const char* code, BuildSeverity severity, std::string message) {
                out.problems.push_back({code, severity, std::move(message), floor.id, canopy.id, std::string(kCheckStep)});
            };
            const std::vector<Vec2> planOutline = pg::withoutRepeatedPoints(pg::pointsOf(canopy.outline));
            if (planOutline.size() < 3 || pg::crossesItself(planOutline) || std::fabs(ringAreaOf(planOutline)) <= 0.01) {
                problem("canopy_outline_invalid", BuildSeverity::Error,
                        "Canopy " + canopy.id + " has an outline that crosses itself or encloses nothing, so it is left out.");
                continue;
            }
            WorldPolygon outline;
            outline.outline = pg::anticlockwise(pg::worldRingOf(planOutline));

            const RoofType* chosen = findRoofType(canopy.roof.type);
            if (!chosen) {
                problem("canopy_type_fallback", BuildSeverity::Warning,
                        "The roof type \"" + canopy.roof.type + "\" of canopy " + canopy.id +
                            " is not known, so the canopy is built flat.");
                chosen = findRoofType(kFlatRoofId);
            }
            if (!chosen) continue;

            const double bottom = level + canopy.height;
            std::vector<WorldPolygon> blocked = blockedBetween(storeys, roofs, bottom, bottom + canopy.thickness);
            CanopyShape shape = shapeOf(canopy, *chosen, outline, bottom, blocked);
            if (!shape.faces.empty()) {
                const std::vector<WorldPolygon> more = blockedBetween(storeys, roofs, bottom, shape.top);
                if (more != blocked) {
                    blocked = more;
                    shape = shapeOf(canopy, *chosen, outline, bottom, blocked);
                }
            }
            if (shape.faces.empty()) {
                problem("canopy_outline_invalid", BuildSeverity::Error,
                        "Canopy " + canopy.id + " lies wholly inside the building or under its roof, so it is left out.");
                continue;
            }
            if (!shape.fallback.empty()) problem("canopy_type_fallback", BuildSeverity::Warning, shape.fallback);
            if (!supported(floor, canopy, planOutline)) {
                problem("canopy_unsupported", BuildSeverity::Warning,
                        "Canopy " + canopy.id + " has no post, wall or deck under it, so it is built as a cantilever.");
            }

            CanopySlots slots;
            const Surface covering = shape.type == kFlatRoofId ? Surface::FlatRoof : Surface::Roof;
            slots.covering = partSlot(project, covering, canopy.material).slot;
            slots.soffit = partSlot(kSoffitSlot, "Soffit", canopy.undersideMaterial).slot;
            slots.fascia = partSlot(kFasciaSlot, "Fascia", canopy.fasciaMaterial).slot;
            slots.wall = partSlot(project, Surface::WallOutside, std::nullopt).slot;
            slots.glass = partSlot(project, Surface::Glass, std::nullopt).slot;

            BuildPart part;
            part.object = floor.id + "_" + canopy.id + "_Canopy";
            part.kind = "canopy";
            part.id = canopy.id;
            for (const RoofFace& face : shape.faces) part.faces.push_back({face.outline, face.holes, slots.of(face.part)});
            collisionOf(part, shape, bottom);
            out.parts.push_back(std::move(part));
        }
        return out;
    }
};

const PartMakerRegistrar registrar(std::make_unique<CanopyMaker>());

}

}
