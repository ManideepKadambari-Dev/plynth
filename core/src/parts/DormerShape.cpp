#include "DormerShape.h"

#include "../ClipperInclude.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <utility>

namespace plynth::dormers {

namespace {

// Decimal places the region checks keep.
constexpr int kDigits = 6;
// Areas below this, in square metres, count as none: a dormer may touch a ridge or an edge.
constexpr double kSliver = 1e-4;
// A t far behind any dormer, for rings that are clipped back to where they meet the main roof.
constexpr double kFar = 100.0;

// "Dormer dm1", or "The dormer" before it has an id, to start a sentence.
std::string subjectOf(const Dormer& d)
{
    return d.id.empty() ? std::string("The dormer") : "Dormer " + d.id;
}

// "dormer dm1", or "the dormer" before it has an id, inside a sentence.
std::string objectOf(const Dormer& d)
{
    return d.id.empty() ? std::string("the dormer") : "dormer " + d.id;
}

// A length in metres with two decimals.
std::string metres(double value)
{
    char text[32];
    std::snprintf(text, sizeof text, "%.2f", value);
    return text;
}

// The shape with a problem.
Shape refused(Shape shape, std::string code, std::string message)
{
    shape.code = std::move(code);
    shape.status = fail(ErrorCode::InvalidValue, std::move(message));
    return shape;
}

// The normal of a ring by Newell's method, not scaled.
Vec3 newellOf(const std::vector<Vec3>& ring)
{
    Vec3 n;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec3& a = ring[i];
        const Vec3& b = ring[(i + 1) % ring.size()];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n;
}

// A ring of a face seen from above.
std::vector<Vec2> flat(const std::vector<Vec3>& ring)
{
    std::vector<Vec2> out;
    out.reserve(ring.size());
    for (const Vec3& p : ring) out.push_back({p.x, p.y});
    return out;
}

// Says whether a world position is on a face seen from above: inside its outline and in none of its holes.
bool onFace(const RoofFace& face, Vec2 p)
{
    const std::vector<Vec2> outline = flat(face.outline);
    if (!insidePolygon(p, outline)) return false;
    for (const std::vector<Vec3>& hole : face.holes) {
        const std::vector<Vec2> ring = flat(hole);
        if (insidePolygon(p, ring)) return false;
    }
    return true;
}

// A ring as a Clipper path.
Clipper2Lib::PathD pathOf(const std::vector<Vec2>& ring)
{
    Clipper2Lib::PathD path;
    path.reserve(ring.size());
    for (const Vec2& p : ring) path.emplace_back(p.x, p.y);
    return path;
}

// A face seen from above as a region.
Clipper2Lib::PathsD regionOf(const RoofFace& face)
{
    Clipper2Lib::PathsD out{pathOf(flat(face.outline))};
    for (const std::vector<Vec3>& hole : face.holes) out.push_back(pathOf(flat(hole)));
    return out;
}

// A plan area as a region in world X and Y.
Clipper2Lib::PathsD regionOf(const PlanArea& area)
{
    const auto world = [](const std::vector<Vec2>& ring) {
        std::vector<Vec2> out;
        for (const Vec2& p : ring) out.push_back(planToWorld(p));
        return out;
    };
    Clipper2Lib::PathsD out{pathOf(world(area.outline))};
    for (const std::vector<Vec2>& hole : area.holes) out.push_back(pathOf(world(hole)));
    return out;
}

// How much of a ring lies outside a region, square metres.
double outsideArea(const std::vector<Vec2>& ring, const Clipper2Lib::PathsD& region)
{
    const Clipper2Lib::PathsD rest =
        Clipper2Lib::Difference(Clipper2Lib::PathsD{pathOf(ring)}, region, Clipper2Lib::FillRule::EvenOdd, kDigits);
    return std::fabs(Clipper2Lib::Area(rest));
}

// How much two rings share, square metres.
double sharedArea(const std::vector<Vec2>& a, const std::vector<Vec2>& b)
{
    const Clipper2Lib::PathsD both = Clipper2Lib::Intersect(Clipper2Lib::PathsD{pathOf(a)}, Clipper2Lib::PathsD{pathOf(b)},
                                                            Clipper2Lib::FillRule::EvenOdd, kDigits);
    return std::fabs(Clipper2Lib::Area(both));
}

// Every ring of a dormer seen from above in world X and Y: where it stands and its own roof.
std::vector<std::vector<Vec2>> worldRings(const Shape& shape)
{
    std::vector<std::vector<Vec2>> out{shape.atRing(shape.footprint)};
    for (const std::vector<Vec2>& slab : shape.slabs) out.push_back(shape.atRing(slab));
    return out;
}

}

// Moves a local point to the plan's world X and Y.
Vec2 Shape::at(Vec2 local) const
{
    return {front.x + across.x * local.x + back.x * local.y, front.y + across.y * local.x + back.y * local.y};
}

// Moves every point of a local ring.
std::vector<Vec2> Shape::atRing(const std::vector<Vec2>& ring) const
{
    std::vector<Vec2> out;
    out.reserve(ring.size());
    for (const Vec2& p : ring) out.push_back(at(p));
    return out;
}

// The main roof's top at a local point.
double Shape::covering(Vec2 local) const
{
    return footZ + slope * local.y;
}

// The main roof's underside at a local point.
double Shape::underside(Vec2 local) const
{
    return covering(local) - kRoofThickness;
}

// The underside of the dormer's own roof at a local point.
double Shape::roofUnder(Vec2 local) const
{
    if (kind == Kind::Shed) return footZ + frontHeight + rise * local.y;
    return footZ + frontHeight + rise * (width / 2.0 - std::fabs(local.x));
}

// Says whether Plynth builds a dormer of this type.
bool knownType(std::string_view type)
{
    return type == "gable" || type == "shed";
}

// The area of a ring by the shoelace rule.
double ringArea(const std::vector<Vec2>& ring)
{
    double twice = 0.0;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec2& a = ring[i];
        const Vec2& b = ring[(i + 1) % ring.size()];
        twice += a.x * b.y - b.x * a.y;
    }
    return twice / 2.0;
}

// Finds the covering face under the dormer, sets up its frame on that face and checks that it fits.
Shape shapeOf(const Dormer& dormer, const Id& floorId, const std::vector<RoofBuild>& roofs,
              const std::vector<RoofArea>& areas)
{
    Shape shape;
    shape.width = dormer.width;
    shape.depth = dormer.depth;
    shape.front = planToWorld({dormer.x, dormer.y});
    if (!knownType(dormer.type)) {
        return refused(shape, "dormer_type_unknown",
                       subjectOf(dormer) + " has the type \"" + dormer.type + "\", which is not gable or shed.");
    }
    shape.kind = dormer.type == "shed" ? Kind::Shed : Kind::Gable;

    const RoofBuild* roof = nullptr;
    const RoofFace* face = nullptr;
    for (const RoofBuild& r : roofs) {
        for (const RoofFace& f : r.faces) {
            if (f.part == RoofPart::Covering && onFace(f, shape.front)) {
                roof = &r;
                face = &f;
                break;
            }
        }
        if (face) break;
    }
    const std::string notOnRoof = subjectOf(dormer) + " does not stand on a pitched roof of floor " + floorId + ".";
    if (!face) return refused(shape, "dormer_not_on_roof", notOnRoof);
    const RoofType* type = findRoofType(roof->type);
    const Vec3 n = newellOf(face->outline);
    if (!type || !type->usesPitch() || !(n.z > 0.0)) return refused(shape, "dormer_not_on_roof", notOnRoof);
    const double gx = -n.x / n.z;
    const double gy = -n.y / n.z;
    const double slope = std::hypot(gx, gy);
    const double degree = std::numbers::pi / 180.0;
    if (slope < std::tan(kMinPitchedRoofPitch * degree) * (1.0 - 1e-6)) {
        return refused(shape, "dormer_not_on_roof", notOnRoof);
    }

    shape.roofIndex = roof->index;
    shape.slope = slope;
    shape.back = {gx / slope, gy / slope};
    shape.across = {shape.back.y, -shape.back.x};
    const Vec3& o = face->outline.front();
    shape.footZ = o.z + gx * (shape.front.x - o.x) + gy * (shape.front.y - o.y);
    shape.rise = std::tan(dormer.pitch * degree);

    if (dormer.windowWidth > dormer.width - 2.0 * kWindowJamb + 1e-9) {
        return refused(shape, "dormer_window_too_big",
                       "The window of " + objectOf(dormer) + " is too wide for its front: it can be at most " +
                           metres(dormer.width - 2.0 * kWindowJamb) + " m wide.");
    }
    const double half = dormer.width / 2.0;
    if (shape.kind == Kind::Shed && shape.rise >= slope - 1e-9) {
        shape.footprint = {{-half, 0.0}, {half, 0.0}, {half, dormer.depth}, {-half, dormer.depth}};
        return refused(shape, "dormer_window_too_big",
                       "A shed dormer needs a lower pitch than the roof it stands on, so " + objectOf(dormer) +
                           " cannot be built there.");
    }

    // The front is as high as depth gives, where the dormer's own roof meets the main roof, but never lower than
    // the window needs; then the dormer reaches further back until its roof meets the main roof.
    const double needed = dormer.windowSill + dormer.windowHeight + kWindowHead;
    const double fall = shape.kind == Kind::Gable ? shape.rise * half : 0.0;
    const double gain = shape.kind == Kind::Gable ? slope : slope - shape.rise;
    shape.frontHeight = std::max(gain * dormer.depth - fall, needed);
    shape.depth = (shape.frontHeight + fall) / gain;
    const bool deeper = shape.depth > dormer.depth + 1e-9;
    if (shape.kind == Kind::Gable) {
        const double cheek = shape.frontHeight / slope;
        shape.footprint = {{-half, 0.0}, {half, 0.0}, {half, cheek}, {0.0, shape.depth}, {-half, cheek}};
    } else {
        shape.footprint = {{-half, 0.0}, {half, 0.0}, {half, shape.depth}, {-half, shape.depth}};
    }

    // Its own roof reaches kDormerOverhang past the front and the cheeks, and back to where its top meets the
    // main roof, so nothing of it stands out behind.
    const double reach = half + kDormerOverhang;
    const auto meets = [&](Vec2 q) { return shape.roofUnder(q) + kRoofThickness - shape.covering(q); };
    if (shape.kind == Kind::Gable) {
        shape.slabs.push_back(clippedRing({{-reach, -kDormerOverhang}, {0.0, -kDormerOverhang}, {0.0, kFar}, {-reach, kFar}}, meets));
        shape.slabs.push_back(clippedRing({{0.0, -kDormerOverhang}, {reach, -kDormerOverhang}, {reach, kFar}, {0.0, kFar}}, meets));
    } else {
        shape.slabs.push_back(clippedRing({{-reach, -kDormerOverhang}, {reach, -kDormerOverhang}, {reach, kFar}, {-reach, kFar}}, meets));
    }

    // Where it stands must be on the face and inside the section; its roof behind the front only on the face.
    const RoofArea* area = nullptr;
    for (const RoofArea& a : areas) {
        if (a.index == roof->index) area = &a;
    }
    const std::vector<Vec2> standing = shape.atRing(shape.footprint);
    const Clipper2Lib::PathsD faceRegion = regionOf(*face);
    bool fits = area && outsideArea(standing, regionOf(area->area)) < kSliver && outsideArea(standing, faceRegion) < kSliver;
    for (const std::vector<Vec2>& slab : shape.slabs) {
        const std::vector<Vec2> behind = clippedRing(slab, [](Vec2 q) { return q.y; });
        if (fits && behind.size() >= 3) fits = outsideArea(shape.atRing(behind), faceRegion) < kSliver;
    }
    if (!fits) {
        const std::string why = deeper ? ", since its window needs it to reach " + metres(shape.depth) + " m back" : std::string();
        return refused(shape, "dormer_too_big",
                       subjectOf(dormer) + " does not fit its roof: it reaches past the ridge, a hip or valley, or the edge " +
                           "of the roof section" + why + ".");
    }
    return shape;
}

// Compares every ring of one dormer with every ring of the other.
bool overlaps(const Shape& a, const Shape& b)
{
    for (const std::vector<Vec2>& ra : worldRings(a)) {
        for (const std::vector<Vec2>& rb : worldRings(b)) {
            if (sharedArea(ra, rb) > kSliver) return true;
        }
    }
    return false;
}

}
