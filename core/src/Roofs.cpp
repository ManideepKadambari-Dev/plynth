#include "plynth/core/Roofs.h"

#include "ClipperInclude.h"
#include "roofs/RoofParts.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <mutex>
#include <numbers>

namespace plynth {

namespace {

constexpr int kPrecision = 8;
constexpr double kMiterLimit = 5.0;
constexpr double kSliver = 1e-6;  // pieces smaller than this many square metres are rounding noise
constexpr double kClose = 1e-6;   // points nearer than this are one point
constexpr double kFar = 1e9;

// The registry of roof types, built on first use so registering from namespace scope works in any start up order.
struct Registry {
    std::mutex mutex;
    std::vector<std::unique_ptr<RoofType>> types;
};

// The one registry.
Registry& registry()
{
    static Registry instance;
    return instance;
}

// Says whether text is a well formed type id: lower case letters, digits and "_", starting with a letter.
bool wellFormedTypeId(std::string_view id)
{
    if (id.empty() || id.size() > 64) return false;
    if (id.front() < 'a' || id.front() > 'z') return false;
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

// The known type ids as one text, for messages.
std::string knownTypeIds()
{
    std::string known;
    for (const RoofType* t : allRoofTypes()) {
        if (!known.empty()) known += ", ";
        known += std::string(t->id());
    }
    return known;
}

// A plan ring as a world ring: y turned into -y and the points reversed so the turning direction is kept.
std::vector<Vec2> worldRing(const std::vector<Vec2>& ring)
{
    std::vector<Vec2> out;
    out.reserve(ring.size());
    for (auto it = ring.rbegin(); it != ring.rend(); ++it) out.push_back({it->x, -it->y});
    return out;
}

// A plan area as a world polygon.
WorldPolygon worldPolygon(const PlanArea& area)
{
    WorldPolygon out;
    out.outline = worldRing(area.outline);
    for (const std::vector<Vec2>& hole : area.holes) out.holes.push_back(worldRing(hole));
    return out;
}

// A list of points as a Clipper path.
Clipper2Lib::PathD toPath(const std::vector<Vec2>& points)
{
    Clipper2Lib::PathD path;
    path.reserve(points.size());
    for (const Vec2& p : points) path.emplace_back(p.x, p.y);
    return path;
}

// The outlines and holes of the polygons as Clipper paths.
Clipper2Lib::PathsD toPaths(const std::vector<WorldPolygon>& polygons)
{
    Clipper2Lib::PathsD paths;
    for (const WorldPolygon& poly : polygons) {
        paths.push_back(toPath(poly.outline));
        for (const std::vector<Vec2>& hole : poly.holes) paths.push_back(toPath(hole));
    }
    return paths;
}

// The ring without repeated points and with the wanted turning direction.
std::vector<Vec2> cleaned(const Clipper2Lib::PathD& path, bool outline)
{
    std::vector<Vec2> ring;
    for (const Clipper2Lib::PointD& p : path) {
        if (!ring.empty() && std::fabs(ring.back().x - p.x) < kClose && std::fabs(ring.back().y - p.y) < kClose) continue;
        ring.push_back({p.x, p.y});
    }
    while (ring.size() > 1 && std::fabs(ring.back().x - ring.front().x) < kClose &&
           std::fabs(ring.back().y - ring.front().y) < kClose) {
        ring.pop_back();
    }
    if (ring.size() >= 3 && (signedArea(ring) > 0.0) != outline) std::reverse(ring.begin(), ring.end());
    return ring;
}

// Collects the pieces of a polygon tree: each outline with the holes directly inside it.
void collect(const Clipper2Lib::PolyPathD& node, std::vector<WorldPolygon>& out)
{
    for (std::size_t i = 0; i < node.Count(); ++i) {
        const Clipper2Lib::PolyPathD& child = *node.Child(i);
        if (!child.IsHole()) {
            WorldPolygon poly;
            poly.outline = cleaned(child.Polygon(), true);
            for (std::size_t h = 0; h < child.Count(); ++h) {
                std::vector<Vec2> hole = cleaned(child.Child(h)->Polygon(), false);
                if (hole.size() >= 3) poly.holes.push_back(std::move(hole));
            }
            double size = signedArea(poly.outline);
            for (const auto& hole : poly.holes) size += signedArea(hole);
            if (poly.outline.size() >= 3 && size >= kSliver) out.push_back(std::move(poly));
        }
        collect(child, out);
    }
}

// Runs one clip operation.
std::vector<WorldPolygon> run(Clipper2Lib::ClipType type, const Clipper2Lib::PathsD& subject, const Clipper2Lib::PathsD& clip)
{
    Clipper2Lib::ClipperD clipper(kPrecision);
    clipper.AddSubject(subject);
    if (!clip.empty()) clipper.AddClip(clip);
    Clipper2Lib::PolyTreeD tree;
    clipper.Execute(type, Clipper2Lib::FillRule::NonZero, tree);
    std::vector<WorldPolygon> out;
    collect(tree, out);
    return out;
}

// The polygons grown by delta (shrunk when negative) with mitred corners.
std::vector<WorldPolygon> offsetBy(const std::vector<WorldPolygon>& polygons, double delta)
{
    const Clipper2Lib::PathsD grown = Clipper2Lib::InflatePaths(toPaths(polygons), delta, Clipper2Lib::JoinType::Miter,
                                                                Clipper2Lib::EndType::Polygon, kMiterLimit, kPrecision);
    return run(Clipper2Lib::ClipType::Union, grown, {});
}

// The distance from a point to a segment.
double distanceToSegment(Vec2 p, Vec2 a, Vec2 b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 0.0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

// Says whether a point is inside the area: in the outline and in none of the holes.
bool insideArea(const PlanArea& area, Vec2 p)
{
    if (!insidePolygon(p, area.outline)) return false;
    for (const std::vector<Vec2>& hole : area.holes) {
        if (insidePolygon(p, hole)) return false;
    }
    return true;
}

// How far a point is from the nearest edge of the area.
double clearance(const PlanArea& area, Vec2 p)
{
    double best = kFar;
    auto scan = [&](const std::vector<Vec2>& ring) {
        for (std::size_t i = 0; i < ring.size(); ++i) {
            best = std::min(best, distanceToSegment(p, ring[i], ring[(i + 1) % ring.size()]));
        }
    };
    scan(area.outline);
    for (const std::vector<Vec2>& hole : area.holes) scan(hole);
    return best;
}

// A position well inside the area: the centre of its outline when that is clear of the edges, else the most
// open point of a grid laid over it.
Vec2 labelPointOf(const PlanArea& area)
{
    double x0 = kFar, x1 = -kFar, y0 = kFar, y1 = -kFar;
    double cx = 0.0, cy = 0.0, twice = 0.0;
    for (std::size_t i = 0; i < area.outline.size(); ++i) {
        const Vec2 a = area.outline[i];
        const Vec2 b = area.outline[(i + 1) % area.outline.size()];
        x0 = std::min(x0, a.x);
        x1 = std::max(x1, a.x);
        y0 = std::min(y0, a.y);
        y1 = std::max(y1, a.y);
        const double cross = a.x * b.y - b.x * a.y;
        twice += cross;
        cx += (a.x + b.x) * cross;
        cy += (a.y + b.y) * cross;
    }
    Vec2 centre{(x0 + x1) / 2.0, (y0 + y1) / 2.0};
    double centreClear = -1.0;
    if (std::fabs(twice) > 1e-12) {
        const Vec2 centroid{cx / (3.0 * twice), cy / (3.0 * twice)};
        if (insideArea(area, centroid)) {
            centre = centroid;
            centreClear = clearance(area, centroid);
        }
    }
    constexpr int kGrid = 24;
    Vec2 gridBest = centre;
    double gridClear = -1.0;
    for (int i = 0; i < kGrid; ++i) {
        for (int j = 0; j < kGrid; ++j) {
            const Vec2 p{x0 + (x1 - x0) * (i + 0.5) / kGrid, y0 + (y1 - y0) * (j + 0.5) / kGrid};
            if (!insideArea(area, p)) continue;
            const double c = clearance(area, p);
            if (c > gridClear) {
                gridClear = c;
                gridBest = p;
            }
        }
    }
    return centreClear >= 0.9 * gridClear ? centre : gridBest;
}

// The index of the section that holds the point, or -1.
int sectionHolding(const std::vector<PlanArea>& sections, Vec2 p)
{
    for (std::size_t i = 0; i < sections.size(); ++i) {
        if (insideArea(sections[i], p)) return static_cast<int>(i);
    }
    return -1;
}

// The roof sections of floor i with the settings that apply to each.
std::vector<RoofArea> areasOfFloor(const Plan& plan, std::size_t floorIndex)
{
    std::vector<RoofArea> out;
    const Floor& floor = plan.floors[floorIndex];
    const std::vector<PlanArea> sections = roofSectionAreas(plan, floorIndex);
    std::vector<const RoofSection*> governing(sections.size(), nullptr);
    for (const RoofSection& entry : plan.roofSections) {
        if (entry.floor != floor.id) continue;
        const int at = sectionHolding(sections, {entry.x, entry.y});
        if (at >= 0 && !governing[static_cast<std::size_t>(at)]) governing[static_cast<std::size_t>(at)] = &entry;
    }
    for (std::size_t i = 0; i < sections.size(); ++i) {
        RoofArea area;
        area.floor = floor.id;
        area.index = static_cast<int>(i);
        area.area = sections[i];
        area.labelPoint = labelPointOf(sections[i]);
        if (governing[i]) {
            area.section = governing[i]->id;
            area.settings = governing[i]->settings;
            area.material = governing[i]->material;
        } else {
            area.settings = plan.roof;
        }
        out.push_back(std::move(area));
    }
    return out;
}

// The world direction of an angle in degrees measured on the plan from +x toward +y.
Vec2 worldDirection(double degrees)
{
    const double r = degrees * std::numbers::pi / 180.0;
    return {std::cos(r), -std::sin(r)};
}

// The direction a roof type gets: the setting, or along the longer side of the section's bounding box.
Vec2 resolveDirection(const RoofSettings& settings, const WorldPolygon& section)
{
    if (settings.direction) return worldDirection(*settings.direction);
    double x0 = kFar, x1 = -kFar, y0 = kFar, y1 = -kFar;
    for (const Vec2& p : section.outline) {
        x0 = std::min(x0, p.x);
        x1 = std::max(x1, p.x);
        y0 = std::min(y0, p.y);
        y1 = std::max(y1, p.y);
    }
    return (x1 - x0) >= (y1 - y0) ? Vec2{1.0, 0.0} : Vec2{0.0, -1.0};
}

// Works out what a type is given: the section, its eaves outline, the base height and the direction.
RoofInput makeInput(const RoofArea& area, const RoofType& type, double base, const std::vector<WorldPolygon>& above)
{
    RoofInput input;
    input.section = worldPolygon(area.area);
    input.settings = area.settings;
    input.base = base;
    input.direction = resolveDirection(area.settings, input.section);
    std::vector<WorldPolygon> eaves{input.section};
    if (type.usesOverhang() && area.settings.overhang > 1e-9) eaves = offsetBy(eaves, area.settings.overhang);
    if (!above.empty()) eaves = run(Clipper2Lib::ClipType::Difference, toPaths(eaves), toPaths(above));
    input.eaves = std::move(eaves);
    return input;
}

// The parameters from 0 to 1, in order, where the break lines cross the edge from p to q.
std::vector<double> cutParameters(Vec2 p, Vec2 q, const std::vector<roofparts::BreakLine>& breaks)
{
    std::vector<double> ts{0.0, 1.0};
    for (const roofparts::BreakLine& line : breaks) {
        const Vec2 n = roofparts::leftOf(line.direction);
        const double fp = n.x * (p.x - line.point.x) + n.y * (p.y - line.point.y);
        const double fq = n.x * (q.x - line.point.x) + n.y * (q.y - line.point.y);
        if ((fp > 1e-9 && fq < -1e-9) || (fp < -1e-9 && fq > 1e-9)) ts.push_back(fp / (fp - fq));
    }
    std::sort(ts.begin(), ts.end());
    return ts;
}

// The point of the edge from p to q at parameter t.
Vec2 along(Vec2 p, Vec2 q, double t)
{
    return {p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t};
}

// Every ring of a polygon: the outline, then the holes.
std::vector<const std::vector<Vec2>*> ringsOf(const WorldPolygon& poly)
{
    std::vector<const std::vector<Vec2>*> rings{&poly.outline};
    for (const std::vector<Vec2>& hole : poly.holes) rings.push_back(&hole);
    return rings;
}

// A face from a polygon with every point lifted to a plane, lowered by drop. Down faces turn every ring.
RoofFace liftedFace(const WorldPolygon& poly, const roofparts::Plane& plane, double drop, bool up, RoofPart part)
{
    RoofFace face;
    face.part = part;
    auto lift = [&](const std::vector<Vec2>& ring) {
        std::vector<Vec3> out;
        for (const Vec2& p : ring) out.push_back({p.x, p.y, plane.at(p) - drop});
        if (!up) std::reverse(out.begin(), out.end());
        return out;
    };
    face.outline = lift(poly.outline);
    for (const std::vector<Vec2>& hole : poly.holes) face.holes.push_back(lift(hole));
    return face;
}

// A vertical face on the edge from p to q, with a bottom and a top height at each end.
RoofFace wallQuad(Vec2 p, Vec2 q, double pBottom, double qBottom, double pTop, double qTop, RoofPart part)
{
    RoofFace face;
    face.part = part;
    face.outline = {{p.x, p.y, pBottom}, {q.x, q.y, qBottom}, {q.x, q.y, qTop}, {p.x, p.y, pTop}};
    return face;
}

// Says whether two points are the same point.
bool samePoint(const Vec3& a, const Vec3& b)
{
    return std::fabs(a.x - b.x) < kClose && std::fabs(a.y - b.y) < kClose && std::fabs(a.z - b.z) < kClose;
}

// The world polygons of a list of plan areas.
std::vector<WorldPolygon> worldPolygons(const std::vector<PlanArea>& areas)
{
    std::vector<WorldPolygon> out;
    for (const PlanArea& a : areas) out.push_back(worldPolygon(a));
    return out;
}

}

namespace roofparts {

std::vector<WorldPolygon> keepHalf(const std::vector<WorldPolygon>& polygons, Vec2 point, Vec2 normal)
{
    double reach = 1.0;
    for (const WorldPolygon& poly : polygons) {
        for (const Vec2& p : poly.outline) reach = std::max(reach, std::hypot(p.x - point.x, p.y - point.y));
    }
    reach *= 4.0;
    const double len = std::hypot(normal.x, normal.y);
    const Vec2 n{normal.x / len, normal.y / len};
    const Vec2 t{n.y, -n.x};
    const Vec2 p0{point.x - t.x * reach, point.y - t.y * reach};
    const Vec2 p1{point.x + t.x * reach, point.y + t.y * reach};
    const Vec2 p2{p1.x + n.x * reach, p1.y + n.y * reach};
    const Vec2 p3{p0.x + n.x * reach, p0.y + n.y * reach};
    const Clipper2Lib::PathsD half{toPath({p0, p1, p2, p3})};
    return run(Clipper2Lib::ClipType::Intersection, toPaths(polygons), half);
}

RoofFace planeFace(const WorldPolygon& polygon, const Plane& plane, double drop, bool up, RoofPart part)
{
    return liftedFace(polygon, plane, drop, up, part);
}

RoofFace wallFace(Vec2 p, Vec2 q, double pBottom, double qBottom, double pTop, double qTop, RoofPart part)
{
    return wallQuad(p, q, pBottom, qBottom, pTop, qTop, part);
}

std::optional<RoofFace> gableFill(Vec2 a, Vec2 b, double base, double ga, double gb)
{
    constexpr double kEps = 1e-9;
    if (ga <= kEps && gb <= kEps) return std::nullopt;
    Vec2 from = a;
    Vec2 to = b;
    double gFrom = ga;
    double gTo = gb;
    if (ga < 0.0) {
        const double t = ga / (ga - gb);
        from = along(a, b, t);
        gFrom = 0.0;
    }
    if (gb < 0.0) {
        const double t = ga / (ga - gb);
        to = along(a, b, t);
        gTo = 0.0;
    }
    RoofFace face;
    face.part = RoofPart::Gable;
    face.outline.push_back({from.x, from.y, base});
    face.outline.push_back({to.x, to.y, base});
    if (gTo > kEps) face.outline.push_back({to.x, to.y, base + gTo});
    if (gFrom > kEps) face.outline.push_back({from.x, from.y, base + gFrom});
    if (face.outline.size() < 3) return std::nullopt;
    return face;
}

std::vector<WorldPolygon> inset(const std::vector<WorldPolygon>& polygons, double distance)
{
    return offsetBy(polygons, -distance);
}

std::vector<WorldPolygon> without(const std::vector<WorldPolygon>& polygons, const std::vector<WorldPolygon>& cut)
{
    if (cut.empty()) return polygons;
    return run(Clipper2Lib::ClipType::Difference, toPaths(polygons), toPaths(cut));
}

std::vector<RoofFace> flatFaces(const std::vector<WorldPolygon>& polygons, double z, bool up, RoofPart part)
{
    std::vector<RoofFace> faces;
    for (const WorldPolygon& poly : polygons) faces.push_back(liftedFace(poly, Plane{0.0, 0.0, z}, 0.0, up, part));
    return faces;
}

std::vector<RoofFace> ringWalls(const std::vector<WorldPolygon>& polygons, double bottom, double top, RoofPart part)
{
    std::vector<RoofFace> faces;
    for (const WorldPolygon& poly : polygons) {
        for (const std::vector<Vec2>* ring : ringsOf(poly)) {
            for (std::size_t i = 0; i < ring->size(); ++i) {
                const Vec2 p = (*ring)[i];
                const Vec2 q = (*ring)[(i + 1) % ring->size()];
                if (std::hypot(q.x - p.x, q.y - p.y) < kClose) continue;
                faces.push_back(wallQuad(p, q, bottom, bottom, top, top, part));
            }
        }
    }
    return faces;
}

std::vector<RoofFace> pitchedFaces(const RoofInput& input, const std::vector<Piece>& pieces,
                                   const std::vector<BreakLine>& breaks, const std::function<double(Vec2)>& height)
{
    std::vector<RoofFace> faces;
    for (const Piece& piece : pieces) {
        for (const WorldPolygon& poly : piece.area) faces.push_back(liftedFace(poly, piece.top, 0.0, true, RoofPart::Covering));
    }
    for (const Piece& piece : pieces) {
        for (const WorldPolygon& poly : piece.area) {
            faces.push_back(liftedFace(poly, piece.top, kRoofThickness, false, RoofPart::Underside));
        }
    }

    for (const WorldPolygon& poly : input.eaves) {
        for (const std::vector<Vec2>* ring : ringsOf(poly)) {
            for (std::size_t i = 0; i < ring->size(); ++i) {
                const Vec2 p = (*ring)[i];
                const Vec2 q = (*ring)[(i + 1) % ring->size()];
                const std::vector<double> ts = cutParameters(p, q, breaks);
                for (std::size_t k = 0; k + 1 < ts.size(); ++k) {
                    const Vec2 a = along(p, q, ts[k]);
                    const Vec2 b = along(p, q, ts[k + 1]);
                    if (std::hypot(b.x - a.x, b.y - a.y) < kClose) continue;
                    const double ta = height(a);
                    const double tb = height(b);
                    faces.push_back(wallQuad(a, b, ta - kRoofThickness, tb - kRoofThickness, ta, tb, RoofPart::Edge));
                }
            }
        }
    }

    // Gable fills: along each edge of the section, wherever the underside is above the base.
    struct Sample {
        double t;
        double g;
    };
    for (const std::vector<Vec2>* ring : ringsOf(input.section)) {
        for (std::size_t i = 0; i < ring->size(); ++i) {
            const Vec2 p = (*ring)[i];
            const Vec2 q = (*ring)[(i + 1) % ring->size()];
            const std::vector<double> ts = cutParameters(p, q, breaks);

            std::vector<Sample> samples;
            for (std::size_t k = 0; k < ts.size(); ++k) {
                const double g = height(along(p, q, ts[k])) - kRoofThickness - input.base;
                if (k > 0) {
                    const Sample prev = samples.back();
                    if ((prev.g > 1e-9 && g < -1e-9) || (prev.g < -1e-9 && g > 1e-9)) {
                        const double at = prev.g / (prev.g - g);
                        samples.push_back({prev.t + (ts[k] - prev.t) * at, 0.0});
                    }
                }
                samples.push_back({ts[k], g});
            }

            std::size_t k = 0;
            while (k < samples.size()) {
                if (samples[k].g < -1e-9) {
                    ++k;
                    continue;
                }
                std::size_t end = k;
                while (end + 1 < samples.size() && samples[end + 1].g >= -1e-9) ++end;
                bool rises = false;
                for (std::size_t m = k; m <= end; ++m) rises = rises || samples[m].g > 1e-9;
                if (rises) {
                    const Vec2 a = along(p, q, samples[k].t);
                    const Vec2 b = along(p, q, samples[end].t);
                    RoofFace face;
                    face.part = RoofPart::Gable;
                    face.outline.push_back({a.x, a.y, input.base});
                    face.outline.push_back({b.x, b.y, input.base});
                    for (std::size_t m = end + 1; m-- > k;) {
                        const Vec2 at = along(p, q, samples[m].t);
                        const Vec3 top{at.x, at.y, input.base + std::max(0.0, samples[m].g)};
                        if (!samePoint(face.outline.back(), top)) face.outline.push_back(top);
                    }
                    while (face.outline.size() > 3 && samePoint(face.outline.back(), face.outline.front())) {
                        face.outline.pop_back();
                    }
                    if (face.outline.size() >= 3) faces.push_back(std::move(face));
                }
                k = end + 1;
            }
        }
    }
    return faces;
}

}

std::vector<RoofArea> roofAreas(const Plan& plan)
{
    std::vector<RoofArea> out;
    for (std::size_t i = 0; i < plan.floors.size(); ++i) {
        std::vector<RoofArea> part = areasOfFloor(plan, i);
        out.insert(out.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
    }
    return out;
}

std::vector<RoofArea> roofAreasOf(const Plan& plan, std::string_view floorId)
{
    for (std::size_t i = 0; i < plan.floors.size(); ++i) {
        if (plan.floors[i].id == floorId) return areasOfFloor(plan, i);
    }
    return {};
}

std::optional<RoofArea> roofAreaAt(const Plan& plan, std::string_view floorId, Vec2 position)
{
    for (RoofArea& area : roofAreasOf(plan, floorId)) {
        if (insideArea(area.area, position)) return std::move(area);
    }
    return std::nullopt;
}

std::vector<Id> unusedRoofSections(const Plan& plan)
{
    std::vector<Id> unused;
    std::vector<std::vector<bool>> taken(plan.floors.size());
    std::vector<std::vector<PlanArea>> sections(plan.floors.size());
    for (std::size_t i = 0; i < plan.floors.size(); ++i) {
        sections[i] = roofSectionAreas(plan, i);
        taken[i].assign(sections[i].size(), false);
    }
    for (const RoofSection& entry : plan.roofSections) {
        std::size_t fi = plan.floors.size();
        for (std::size_t i = 0; i < plan.floors.size(); ++i) {
            if (plan.floors[i].id == entry.floor) fi = i;
        }
        if (fi == plan.floors.size()) {
            unused.push_back(entry.id);
            continue;
        }
        const int at = sectionHolding(sections[fi], {entry.x, entry.y});
        if (at < 0 || taken[fi][static_cast<std::size_t>(at)]) {
            unused.push_back(entry.id);
            continue;
        }
        taken[fi][static_cast<std::size_t>(at)] = true;
    }
    return unused;
}

bool registerRoofType(std::unique_ptr<RoofType> type)
{
    if (!type || !wellFormedTypeId(type->id())) return false;
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    for (const auto& existing : r.types) {
        if (existing->id() == type->id()) return false;
    }
    r.types.push_back(std::move(type));
    return true;
}

RoofTypeRegistrar::RoofTypeRegistrar(std::unique_ptr<RoofType> type)
{
    registerRoofType(std::move(type));
}

const RoofType* findRoofType(std::string_view id)
{
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    for (const auto& type : r.types) {
        if (type->id() == id) return type.get();
    }
    return nullptr;
}

std::vector<const RoofType*> allRoofTypes()
{
    Registry& r = registry();
    std::vector<const RoofType*> out;
    {
        std::lock_guard lock(r.mutex);
        for (const auto& type : r.types) out.push_back(type.get());
    }
    std::sort(out.begin(), out.end(), [](const RoofType* a, const RoofType* b) {
        if (a->sortKey() != b->sortKey()) return a->sortKey() < b->sortKey();
        return a->id() < b->id();
    });
    return out;
}

Status checkRoofSettings(const RoofSettings& settings)
{
    const RoofType* type = findRoofType(settings.type);
    if (!type) {
        return fail(ErrorCode::InvalidValue,
                    "The roof type \"" + settings.type + "\" is not known. Known types: " + knownTypeIds() + ".");
    }
    if (!(std::isfinite(settings.pitch) && settings.pitch >= 0.0 && settings.pitch <= kMaxRoofPitch)) {
        return fail(ErrorCode::InvalidValue, "The roof pitch must be from 0 to 75 degrees.");
    }
    if (type->usesPitch() && settings.pitch < kMinPitchedRoofPitch) {
        return fail(ErrorCode::InvalidValue,
                    "A " + std::string(type->name()) + " roof needs a pitch of at least 5 degrees.");
    }
    if (!(std::isfinite(settings.overhang) && settings.overhang >= 0.0 && settings.overhang <= kMaxRoofOverhang)) {
        return fail(ErrorCode::InvalidValue, "The roof overhang must be from 0 to 2 metres.");
    }
    if (!(std::isfinite(settings.parapet) && settings.parapet >= 0.0 && settings.parapet <= kMaxParapet)) {
        return fail(ErrorCode::InvalidValue, "The parapet height must be from 0 to 3 metres.");
    }
    if (settings.direction &&
        !(std::isfinite(*settings.direction) && *settings.direction >= 0.0 && *settings.direction < 360.0)) {
        return fail(ErrorCode::InvalidValue, "The roof direction must be from 0 up to 360 degrees.");
    }
    return {};
}

FloorRoofs floorRoofs(const Plan& plan, std::size_t floorIndex, const std::vector<double>& levels)
{
    FloorRoofs out;
    if (floorIndex >= plan.floors.size()) return out;
    const Floor& floor = plan.floors[floorIndex];
    const double level = floorIndex < levels.size() ? levels[floorIndex] : 0.0;
    const double base = level + floorRise(floor);
    std::vector<WorldPolygon> above;
    if (floorIndex + 1 < plan.floors.size()) above = worldPolygons(floorFootprint(plan.floors[floorIndex + 1]));

    for (const RoofArea& area : areasOfFloor(plan, floorIndex)) {
        const RoofType* type = findRoofType(area.settings.type);
        if (!type) {
            out.problems.push_back({floor.id, area.section, "roof_type_unknown",
                                    "The roof type \"" + area.settings.type +
                                        "\" is not known, so this roof section is built flat."});
            type = findRoofType(kFlatRoofId);
        } else {
            const Status fits = type->fits(makeInput(area, *type, base, above));
            if (!fits.ok()) {
                out.problems.push_back({floor.id, area.section, "roof_type_fallback", fits.message});
                const RoofType* fallback = findRoofType(type->fallback());
                type = fallback ? fallback : findRoofType(kFlatRoofId);
            }
        }

        RoofBuild build;
        build.floor = floor.id;
        build.index = area.index;
        build.section = area.section;
        if (type) {
            build.type = std::string(type->id());
            build.faces = type->build(makeInput(area, *type, base, above));
        }
        out.roofs.push_back(std::move(build));
    }

    for (const Id& id : unusedRoofSections(plan)) {
        const RoofSection* entry = findRoofSection(plan, id);
        if (!entry || entry->floor != floor.id) continue;
        out.problems.push_back({floor.id, id, "roof_section_unused",
                                "The roof entry " + id + " is not in any roof section, so it has no effect."});
    }
    return out;
}

}
