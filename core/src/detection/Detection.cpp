#include "plynth/core/detection/Detection.h"

#include "plynth/core/ProjectFiles.h"
#include "plynth/core/Rooms.h"
#include "plynth/core/WallGeometry.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <string>
#include <utility>

namespace plynth {

namespace {

constexpr double kMinPointGap = 0.001; // metres between two proposed points
constexpr double kPlaceStep = 0.05;    // metres between the places markExisting checks along a wall
constexpr double kMinTolerance = 0.05; // smallest tolerance markExisting uses, metres
constexpr double kMaxAngleDegrees = 5.0;

// Makes an InvalidValue status.
Status invalid(std::string message)
{
    return fail(ErrorCode::InvalidValue, std::move(message));
}

// Says whether both coordinates are finite.
bool isFinite(Vec2 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y);
}

// Cross product of two vectors.
double cross(Vec2 a, Vec2 b)
{
    return a.x * b.y - a.y * b.x;
}

// Dot product of two vectors.
double dot(Vec2 a, Vec2 b)
{
    return a.x * b.x + a.y * b.y;
}

// Says whether two segments cross each other strictly inside both.
bool properlyCross(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
{
    const Vec2 ab{b.x - a.x, b.y - a.y};
    const Vec2 cd{d.x - c.x, d.y - c.y};
    const double d1 = cross(ab, {c.x - a.x, c.y - a.y});
    const double d2 = cross(ab, {d.x - a.x, d.y - a.y});
    const double d3 = cross(cd, {a.x - c.x, a.y - c.y});
    const double d4 = cross(cd, {b.x - c.x, b.y - c.y});
    constexpr double eps = 1e-12;
    return ((d1 > eps && d2 < -eps) || (d1 < -eps && d2 > eps)) && ((d3 > eps && d4 < -eps) || (d3 < -eps && d4 > eps));
}

// Names a wall for a message, one based as people count.
std::string wallName(std::size_t index)
{
    return "wall " + std::to_string(index + 1);
}

// A zero padded id, so ids sort in the same order as indexes.
std::string paddedId(char letter, std::size_t index)
{
    const std::string digits = std::to_string(index);
    return std::string(1, letter) + std::string(digits.size() < 7 ? 7 - digits.size() : 0, '0') + digits;
}

}

// Checks every rule of the settings and names the first one broken.
Status checkDetectionParams(const DetectionParams& p)
{
    const auto finite = [](double v) { return std::isfinite(v); };
    if (!finite(p.minThickness) || !finite(p.maxThickness) || !finite(p.sensitivity) || !finite(p.minLength)
        || !finite(p.maxOpeningWidth)) {
        return invalid("The detection settings must be numbers.");
    }
    if (p.minThickness <= 0.0) return invalid("The thinnest wall must be above zero.");
    if (p.maxThickness <= 0.0) return invalid("The thickest wall must be above zero.");
    if (p.minThickness >= p.maxThickness) return invalid("The thinnest wall must be thinner than the thickest wall.");
    if (p.minLength <= 0.0) return invalid("The shortest wall must be above zero.");
    if (p.maxOpeningWidth < 0.0) return invalid("The widest opening cannot be below zero.");
    if (p.sensitivity < 0.0 || p.sensitivity > 1.0) return invalid("The sensitivity must be from 0 to 1.");
    return {};
}

// Checks the rules of a valid result and names the first one broken.
Status checkDetectionResult(const DetectionResult& r)
{
    const std::size_t pointCount = r.points.size();
    for (std::size_t i = 0; i < pointCount; ++i) {
        if (!isFinite(r.points[i])) return invalid("Point " + std::to_string(i + 1) + " is not a finite position.");
    }

    std::vector<std::size_t> order(pointCount);
    for (std::size_t i = 0; i < pointCount; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return r.points[a].x < r.points[b].x; });
    for (std::size_t i = 0; i < pointCount; ++i) {
        for (std::size_t j = i + 1; j < pointCount; ++j) {
            const Vec2 a = r.points[order[i]];
            const Vec2 b = r.points[order[j]];
            if (b.x - a.x >= kMinPointGap) break;
            if (distance(a, b) < kMinPointGap) {
                return invalid("Points " + std::to_string(order[i] + 1) + " and " + std::to_string(order[j] + 1)
                               + " are less than 1 mm apart.");
            }
        }
    }

    std::set<std::pair<std::size_t, std::size_t>> pairs;
    for (std::size_t i = 0; i < r.walls.size(); ++i) {
        const ProposedWall& w = r.walls[i];
        if (w.from >= pointCount || w.to >= pointCount) return invalid(wallName(i) + " uses a point that does not exist.");
        if (w.from == w.to) return invalid(wallName(i) + " joins a point to itself.");
        if (!std::isfinite(w.thickness) || w.thickness <= 0.0) return invalid(wallName(i) + " has no thickness above zero.");
        if (!pairs.insert({std::min(w.from, w.to), std::max(w.from, w.to)}).second) {
            return invalid("More than one wall joins the same two points, at " + wallName(i) + ".");
        }
    }

    struct Box {
        double x0, y0, x1, y1;
    };
    std::vector<Box> boxes(r.walls.size());
    for (std::size_t i = 0; i < r.walls.size(); ++i) {
        const Vec2 a = r.points[r.walls[i].from];
        const Vec2 b = r.points[r.walls[i].to];
        boxes[i] = {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
    }

    for (std::size_t i = 0; i < r.walls.size(); ++i) {
        const ProposedWall& w = r.walls[i];
        const Vec2 a = r.points[w.from];
        const Vec2 b = r.points[w.to];
        const Box& box = boxes[i];
        for (std::size_t p = 0; p < pointCount; ++p) {
            if (p == w.from || p == w.to) continue;
            const Vec2 q = r.points[p];
            if (q.x < box.x0 - kMinPointGap || q.x > box.x1 + kMinPointGap || q.y < box.y0 - kMinPointGap
                || q.y > box.y1 + kMinPointGap) {
                continue;
            }
            if (projectOntoSegment(q, a, b).distance < kMinPointGap) {
                return invalid("Point " + std::to_string(p + 1) + " lies on " + wallName(i) + " but is not one of its ends.");
            }
        }
        for (std::size_t j = i + 1; j < r.walls.size(); ++j) {
            const ProposedWall& o = r.walls[j];
            if (o.from == w.from || o.from == w.to || o.to == w.from || o.to == w.to) continue;
            const Box& ob = boxes[j];
            if (ob.x1 < box.x0 || ob.x0 > box.x1 || ob.y1 < box.y0 || ob.y0 > box.y1) continue;
            if (properlyCross(a, b, r.points[o.from], r.points[o.to])) {
                return invalid(wallName(i) + " and " + wallName(j) + " cross without sharing a point.");
            }
        }
    }

    for (std::size_t i = 0; i < r.openings.size(); ++i) {
        const ProposedOpening& o = r.openings[i];
        const std::string name = "Opening " + std::to_string(i + 1);
        if (o.wall >= r.walls.size()) return invalid(name + " is on a wall that does not exist.");
        if (!std::isfinite(o.offset) || o.offset < 0.0) return invalid(name + " has no valid place along its wall.");
        if (!std::isfinite(o.width) || o.width <= 0.0) return invalid(name + " has no width above zero.");
        const Vec2 a = r.points[r.walls[o.wall].from];
        const Vec2 b = r.points[r.walls[o.wall].to];
        if (o.offset > distance(a, b) + 1e-9) return invalid(name + " is past the end of its wall.");
    }

    for (std::size_t i = 0; i < r.rooms.size(); ++i) {
        const ProposedRoom& room = r.rooms[i];
        const std::string name = "Room " + std::to_string(i + 1);
        for (std::size_t w : room.walls) {
            if (w >= r.walls.size()) return invalid(name + " uses a wall that does not exist.");
        }
        if (!isFinite(room.inside) || !std::isfinite(room.centreLineArea)) {
            return invalid(name + " has a position or area that is not finite.");
        }
    }
    return {};
}

// Finds the closed areas of the proposed walls on a temporary floor and turns them into room proposals.
void proposeRooms(DetectionResult& result)
{
    result.rooms.clear();
    if (result.cancelled || !result.status.ok()) return;

    Floor floor;
    floor.id = "f1";
    floor.points.reserve(result.points.size());
    for (std::size_t i = 0; i < result.points.size(); ++i) {
        floor.points.push_back({paddedId('p', i), result.points[i].x, result.points[i].y, 0.0});
    }
    for (std::size_t i = 0; i < result.walls.size(); ++i) {
        Wall w;
        w.id = paddedId('w', i);
        w.from = paddedId('p', result.walls[i].from);
        w.to = paddedId('p', result.walls[i].to);
        w.thickness = result.walls[i].thickness;
        floor.walls.push_back(std::move(w));
    }

    for (const ClosedArea& area : findClosedAreas(floor)) {
        ProposedRoom room;
        for (const Id& id : area.walls) {
            room.walls.push_back(static_cast<std::size_t>(std::stoull(id.substr(1))));
        }
        room.rings = area.rings;
        room.centreLineArea = area.centreLineArea;
        room.inside = area.inside;
        result.rooms.push_back(std::move(room));
    }
}

namespace {

// A straight wall of the floor, ready for the markExisting checks.
struct ExistingSegment {
    Vec2 a;
    Vec2 direction; // unit length
    double length = 0.0;
    double thickness = 0.0;
};

// Collects the straight walls of the floor that have a length.
std::vector<ExistingSegment> straightWalls(const Floor& floor)
{
    std::vector<ExistingSegment> out;
    for (const Wall& w : floor.walls) {
        if (w.bulge != 0.0) continue;
        const Point* a = findPoint(floor, w.from);
        const Point* b = findPoint(floor, w.to);
        if (!a || !b) continue;
        const double length = distance(positionOf(*a), positionOf(*b));
        if (length <= 0.0) continue;
        out.push_back({positionOf(*a), {(b->x - a->x) / length, (b->y - a->y) / length}, length, w.thickness});
    }
    return out;
}

// Says whether a place on a proposed wall lies on an existing wall running the same way.
bool placeCovered(const std::vector<ExistingSegment>& existing, Vec2 place, Vec2 direction, double thickness)
{
    const double maxSin = std::sin(kMaxAngleDegrees * std::numbers::pi / 180.0);
    for (const ExistingSegment& e : existing) {
        if (std::abs(cross(direction, e.direction)) > maxSin) continue;
        const Vec2 rel{place.x - e.a.x, place.y - e.a.y};
        const double along = dot(rel, e.direction);
        if (along < -1e-9 || along > e.length + 1e-9) continue;
        const double tolerance = std::max(0.5 * std::max(thickness, e.thickness), kMinTolerance);
        if (std::abs(cross(rel, e.direction)) <= tolerance) return true;
    }
    return false;
}

}

// Copies a detected result into plan coordinates by adding the floor's image offset to every position.
DetectionResult proposalsOnPlan(const Floor& floor, DetectionResult detected)
{
    if (floor.imageOffset == ImageOffset{}) return detected;
    const double ppm = detected.pixelsPerMetre > 0.0 ? detected.pixelsPerMetre : 1.0;
    const auto move = [&](Vec2& p) { p = imageToPlan(floor, ppm, {p.x * ppm, p.y * ppm}); };
    for (Vec2& p : detected.points) move(p);
    for (ProposedRoom& room : detected.rooms) {
        move(room.inside);
        for (std::vector<Vec2>& ring : room.rings) {
            for (Vec2& p : ring) move(p);
        }
    }
    return detected;
}

// Marks every proposed wall and room against what the floor already has.
void markExisting(const Floor& floor, DetectionResult& result)
{
    const std::vector<ExistingSegment> existing = straightWalls(floor);
    for (ProposedWall& w : result.walls) {
        w.state = ProposalState::New;
        if (w.from >= result.points.size() || w.to >= result.points.size() || existing.empty()) continue;
        const Vec2 a = result.points[w.from];
        const Vec2 b = result.points[w.to];
        const double length = distance(a, b);
        if (length <= 0.0) continue;
        const Vec2 direction{(b.x - a.x) / length, (b.y - a.y) / length};
        const double tolerance = std::max(0.5 * w.thickness, kMinTolerance);

        std::vector<double> places;
        if (length <= 2.0 * tolerance) {
            places.push_back(length / 2.0);
        } else {
            for (double t = tolerance; t < length - tolerance; t += kPlaceStep) places.push_back(t);
            places.push_back(length - tolerance);
        }
        std::size_t covered = 0;
        for (double t : places) {
            const Vec2 place{a.x + direction.x * t, a.y + direction.y * t};
            if (placeCovered(existing, place, direction, w.thickness)) ++covered;
        }
        if (covered == places.size()) {
            w.state = ProposalState::Existing;
        } else if (covered > 0) {
            w.state = ProposalState::Overlaps;
        }
    }
    for (ProposedRoom& room : result.rooms) {
        room.state = roomAt(floor, room.inside).empty() ? ProposalState::New : ProposalState::Existing;
    }
    for (ProposedOpening& o : result.openings) {
        o.state = ProposalState::New;
        if (o.wall >= result.walls.size()) continue;
        const ProposedWall& pw = result.walls[o.wall];
        if (pw.from >= result.points.size() || pw.to >= result.points.size()) continue;
        const Vec2 a = result.points[pw.from];
        const Vec2 b = result.points[pw.to];
        const double length = distance(a, b);
        if (length <= 0.0) continue;
        const Vec2 centre{a.x + (b.x - a.x) * o.offset / length, a.y + (b.y - a.y) * o.offset / length};
        for (const Opening& existingOpening : floor.openings) {
            const Wall* wall = findWall(floor, existingOpening.wall);
            if (!wall) continue;
            const WallCurve curve = wallCurve(floor, *wall);
            if (!curve.valid) continue;
            const CurveProjection projection = projectOntoCurve(centre, curve);
            const double tolerance = std::max(0.5 * std::max(pw.thickness, wall->thickness), kMinTolerance);
            if (projection.distance <= tolerance && std::abs(projection.offset - existingOpening.offset) <= existingOpening.width / 2.0) {
                o.state = ProposalState::Existing;
                break;
            }
        }
    }
}

// Counts the proposals by kind and state.
ProposalCounts countProposals(const DetectionResult& result)
{
    ProposalCounts c;
    c.points = result.points.size();
    c.walls = result.walls.size();
    for (const ProposedWall& w : result.walls) {
        if (w.state == ProposalState::New) ++c.newWalls;
        else if (w.state == ProposalState::Existing) ++c.existingWalls;
        else ++c.overlappingWalls;
    }
    c.rooms = result.rooms.size();
    for (const ProposedRoom& r : result.rooms) {
        if (r.state == ProposalState::New) ++c.newRooms;
    }
    c.openings = result.openings.size();
    for (const ProposedOpening& o : result.openings) {
        if (o.state == ProposalState::New) ++c.newOpenings;
        else if (o.state == ProposalState::Existing) ++c.existingOpenings;
    }
    return c;
}

// Finds the image file and scale of a floor, or says why detection cannot run.
DetectionSource detectionSource(const Plan& plan, const std::filesystem::path& projectFolder, std::string_view floorId)
{
    DetectionSource source;
    const Floor* floor = findFloor(plan, floorId);
    if (!floor) {
        source.status = fail(ErrorCode::NotFound, "There is no floor " + std::string(floorId) + ".");
        return source;
    }
    if (!floor->image) {
        source.status = fail(ErrorCode::NotFound, "The floor " + floor->id + " has no plan image.");
        return source;
    }
    if (!plan.scale.pixelsPerMetre) {
        source.status = fail(ErrorCode::InvalidValue, "Set the scale first.");
        return source;
    }
    source.imageFile = resolveImagePath(projectFolder, *floor->image);
    source.pixelsPerMetre = *plan.scale.pixelsPerMetre;
    return source;
}

}
