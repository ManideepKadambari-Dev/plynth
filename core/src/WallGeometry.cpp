#include "plynth/core/WallGeometry.h"

#include "BandOutline.h"

#include "ClipperInclude.h"

#include <algorithm>
#include <cmath>

namespace plynth {

using detail::BandEnd;
using detail::bandAround;
using detail::dot;
using detail::plus;
using detail::unit;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTiny = 1e-9;
constexpr int kClipperPrecision = 8;

// Says whether two positions are the same place for practical purposes.
bool samePlace(Vec2 a, Vec2 b)
{
    return std::fabs(a.x - b.x) < kTiny && std::fabs(a.y - b.y) < kTiny;
}

// Removes a point that repeats the one before it.
void dedupe(std::vector<Vec2>& line)
{
    std::vector<Vec2> out;
    out.reserve(line.size());
    for (const Vec2& p : line) {
        if (out.empty() || !samePlace(out.back(), p)) out.push_back(p);
    }
    line = std::move(out);
}

// Says whether a wall counts as straight.
bool straightWall(const Wall& wall)
{
    return std::fabs(wall.bulge) < kMinBulge;
}

// The point on a circle at an angle.
Vec2 onCircle(Vec2 centre, double radius, double angle)
{
    return {centre.x + radius * std::cos(angle), centre.y + radius * std::sin(angle)};
}

// An arc of a circle from one point to another, ends exact, cut by arcSegmentCount. sweep is signed.
std::vector<Vec2> arcBetween(Vec2 centre, double radius, double startAngle, double sweep, Vec2 first, Vec2 last)
{
    const int n = arcSegmentCount(radius, sweep);
    std::vector<Vec2> out;
    out.reserve(static_cast<std::size_t>(n) + 1);
    out.push_back(first);
    for (int k = 1; k < n; ++k) out.push_back(onCircle(centre, radius, startAngle + sweep * k / n));
    out.push_back(last);
    return out;
}

// The middle of a rounded corner's arc: the place on the circle in line with the corner point.
Vec2 arcMiddle(const CornerRounding& r, Vec2 corner)
{
    const Vec2 away = unit({corner.x - r.centre.x, corner.y - r.centre.y});
    return plus(r.centre, away, r.radius);
}

// Where a half of a corner arc lies: the arc from where it meets a wall to the arc's middle.
std::vector<Vec2> halfCornerArc(const CornerRounding& r, Vec2 corner, Vec2 meets)
{
    const Vec2 middle = arcMiddle(r, corner);
    const double a0 = std::atan2(meets.y - r.centre.y, meets.x - r.centre.x);
    double sweep = std::atan2(middle.y - r.centre.y, middle.x - r.centre.x) - a0;
    while (sweep > kPi) sweep -= 2.0 * kPi;
    while (sweep < -kPi) sweep += 2.0 * kPi;
    return arcBetween(r.centre, r.radius, a0, sweep, meets, middle);
}

// Every wall that touches a point, in the order of the floor's wall list.
std::vector<const Wall*> wallsAt(const Floor& floor, const Point& point)
{
    std::vector<const Wall*> out;
    for (const Wall& w : floor.walls) {
        if (w.from == point.id || w.to == point.id) out.push_back(&w);
    }
    return out;
}

}

// The bend of the wall between two positions.
WallCurve curveBetween(Vec2 from, Vec2 to, double bulge)
{
    WallCurve c;
    c.from = from;
    c.to = to;
    c.chord = distance(from, to);
    if (!(c.chord >= kTiny) || !std::isfinite(bulge)) return c;
    c.valid = true;
    c.length = c.chord;
    if (std::fabs(bulge) < kMinBulge) return c;

    c.curved = true;
    c.bulge = bulge;
    const double b = std::fabs(bulge);
    c.radius = (c.chord * c.chord / 4.0 + b * b) / (2.0 * b);
    const Vec2 d = unit({to.x - from.x, to.y - from.y});
    const Vec2 right = rightOf(d);
    const Vec2 mid{(from.x + to.x) / 2.0, (from.y + to.y) / 2.0};
    const double sign = bulge > 0.0 ? 1.0 : -1.0;
    c.centre = plus(mid, right, bulge - sign * c.radius);
    c.startAngle = std::atan2(from.y - c.centre.y, from.x - c.centre.x);
    c.sweep = -sign * 4.0 * std::atan(2.0 * b / c.chord);
    c.length = c.radius * std::fabs(c.sweep);
    return c;
}

// The true centre line of a wall.
WallCurve wallCurve(const Floor& floor, const Wall& wall)
{
    const Point* a = findPoint(floor, wall.from);
    const Point* b = findPoint(floor, wall.to);
    if (!a || !b) return {};
    return curveBetween(positionOf(*a), positionOf(*b), wall.bulge);
}

// The place at a distance along the curve.
Vec2 pointAlong(const WallCurve& curve, double offset)
{
    offset = std::clamp(offset, 0.0, curve.length);
    if (!curve.valid) return curve.from;
    if (!curve.curved) {
        const double t = offset / curve.length;
        return {curve.from.x + (curve.to.x - curve.from.x) * t, curve.from.y + (curve.to.y - curve.from.y) * t};
    }
    if (offset == 0.0) return curve.from;
    if (offset == curve.length) return curve.to;
    return onCircle(curve.centre, curve.radius, curve.startAngle + curve.sweep * offset / curve.length);
}

// The direction of the curve at a distance along it.
Vec2 tangentAlong(const WallCurve& curve, double offset)
{
    if (!curve.valid) return {1.0, 0.0};
    if (!curve.curved) return unit({curve.to.x - curve.from.x, curve.to.y - curve.from.y});
    offset = std::clamp(offset, 0.0, curve.length);
    const double a = curve.startAngle + curve.sweep * offset / curve.length;
    const double s = curve.sweep > 0.0 ? 1.0 : -1.0;
    return {-s * std::sin(a), s * std::cos(a)};
}

// The closest place on the curve to a position.
CurveProjection projectOntoCurve(Vec2 position, const WallCurve& curve)
{
    CurveProjection r;
    r.position = curve.from;
    if (!curve.valid) {
        r.distance = distance(position, curve.from);
        return r;
    }
    if (!curve.curved) {
        const SegmentProjection p = projectOntoSegment(position, curve.from, curve.to);
        r.position = p.position;
        r.offset = p.offset;
        r.distance = p.distance;
    } else {
        const double theta = std::atan2(position.y - curve.centre.y, position.x - curve.centre.x);
        const double dir = curve.sweep > 0.0 ? 1.0 : -1.0;
        double u = (theta - curve.startAngle) * dir;
        u -= 2.0 * kPi * std::floor(u / (2.0 * kPi));
        const double span = std::fabs(curve.sweep);
        if (u <= span) {
            r.offset = curve.radius * u;
            r.position = onCircle(curve.centre, curve.radius, theta);
            r.distance = distance(position, r.position);
        } else {
            const double toFrom = distance(position, curve.from);
            const double toTo = distance(position, curve.to);
            if (toFrom <= toTo) {
                r.offset = 0.0;
                r.position = curve.from;
                r.distance = toFrom;
            } else {
                r.offset = curve.length;
                r.position = curve.to;
                r.distance = toTo;
            }
        }
    }
    const Vec2 tangent = tangentAlong(curve, r.offset);
    const double side = dot({position.x - r.position.x, position.y - r.position.y}, rightOf(tangent));
    r.side = side >= 0.0 ? WallSide::Right : WallSide::Left;
    return r;
}

// The curve between two distances along it as a polyline.
std::vector<Vec2> curvePoints(const WallCurve& curve, double fromOffset, double toOffset)
{
    fromOffset = std::clamp(fromOffset, 0.0, curve.length);
    toOffset = std::clamp(toOffset, 0.0, curve.length);
    const Vec2 first = pointAlong(curve, fromOffset);
    const Vec2 last = pointAlong(curve, toOffset);
    if (!curve.valid || !curve.curved) return {first, last};
    const double sweep = curve.sweep * (toOffset - fromOffset) / curve.length;
    const int n = arcSegmentCount(curve.radius, sweep);
    std::vector<Vec2> out;
    out.reserve(static_cast<std::size_t>(n) + 1);
    out.push_back(first);
    for (int k = 1; k < n; ++k) out.push_back(pointAlong(curve, fromOffset + (toOffset - fromOffset) * k / n));
    out.push_back(last);
    return out;
}

// The bulge that puts the arc's middle nearest a position.
double bulgeFromPosition(Vec2 from, Vec2 to, Vec2 position)
{
    const double chord = distance(from, to);
    if (!(chord > 0.0)) return 0.0;
    const Vec2 d = unit({to.x - from.x, to.y - from.y});
    const Vec2 mid{(from.x + to.x) / 2.0, (from.y + to.y) / 2.0};
    const double b = dot({position.x - mid.x, position.y - mid.y}, rightOf(d));
    const double limit = kMaxBulgeRatio * chord;
    return std::clamp(b, -limit, limit);
}

// The side of a wall a position is on.
WallSide wallSideOf(const Floor& floor, const Wall& wall, Vec2 position)
{
    return projectOntoCurve(position, wallCurve(floor, wall)).side;
}

// The wall's own height when set, otherwise the floor's ceiling height.
double wallHeight(const Floor& floor, const Wall& wall)
{
    return wall.height ? *wall.height : floor.ceilingHeight;
}

// Works out how a point is rounded.
CornerRounding cornerRounding(const Floor& floor, const Point& point)
{
    CornerRounding r;
    if (!(point.cornerRadius > 0.0) || !std::isfinite(point.cornerRadius)) return r;
    const Point* onFloor = findPoint(floor, point.id);
    if (!onFloor) return r;
    const std::vector<const Wall*> walls = wallsAt(floor, point);
    if (walls.size() != 2) return r;
    if (!straightWall(*walls[0]) || !straightWall(*walls[1])) return r;

    const Vec2 corner = positionOf(*onFloor);
    Vec2 toward[2];
    double length[2];
    for (int i = 0; i < 2; ++i) {
        const Id& otherId = walls[static_cast<std::size_t>(i)]->from == point.id ? walls[static_cast<std::size_t>(i)]->to
                                                                                 : walls[static_cast<std::size_t>(i)]->from;
        const Point* other = findPoint(floor, otherId);
        if (!other) return r;
        const Vec2 delta{other->x - corner.x, other->y - corner.y};
        length[i] = std::hypot(delta.x, delta.y);
        if (!(length[i] > kTiny)) return r;
        toward[i] = unit(delta);
    }
    const double cosAngle = std::clamp(dot(toward[0], toward[1]), -1.0, 1.0);
    const double angle = std::acos(cosAngle);
    constexpr double degree = kPi / 180.0;
    if (angle < degree || angle > kPi - degree) return r;

    const double tanHalf = std::tan(angle / 2.0);
    const double radius = std::min(point.cornerRadius, tanHalf * std::min(length[0], length[1]) / 2.0);
    if (!(radius > 0.0)) return r;

    r.rounded = true;
    r.radius = radius;
    r.trim = radius / tanHalf;
    r.firstWall = walls[0]->id;
    r.secondWall = walls[1]->id;
    const Vec2 bisector = unit({toward[0].x + toward[1].x, toward[0].y + toward[1].y});
    r.centre = plus(corner, bisector, radius / std::sin(angle / 2.0));
    r.onFirst = plus(corner, toward[0], r.trim);
    r.onSecond = plus(corner, toward[1], r.trim);
    const double a0 = std::atan2(r.onFirst.y - r.centre.y, r.onFirst.x - r.centre.x);
    double sweep = std::atan2(r.onSecond.y - r.centre.y, r.onSecond.x - r.centre.x) - a0;
    while (sweep > kPi) sweep -= 2.0 * kPi;
    while (sweep < -kPi) sweep += 2.0 * kPi;
    r.arc = arcBetween(r.centre, r.radius, a0, sweep, r.onFirst, r.onSecond);
    return r;
}

// Works out the path of a wall.
WallPath wallPath(const Floor& floor, const Wall& wall)
{
    WallPath path;
    path.curve = wallCurve(floor, wall);
    if (!path.curve.valid) return path;

    const Point* from = findPoint(floor, wall.from);
    const Point* to = findPoint(floor, wall.to);
    CornerRounding startRound = cornerRounding(floor, *from);
    CornerRounding endRound = cornerRounding(floor, *to);
    path.roundedStart = startRound.rounded && (startRound.firstWall == wall.id || startRound.secondWall == wall.id);
    path.roundedEnd = endRound.rounded && (endRound.firstWall == wall.id || endRound.secondWall == wall.id);
    if (path.roundedStart) path.startTrim = startRound.trim;
    if (path.roundedEnd) path.endTrim = endRound.trim;

    const double length = path.curve.length;
    const double begin = path.startTrim;
    const double finish = std::max(begin, length - path.endTrim);

    std::vector<Vec2> line;
    if (path.roundedStart) {
        const Vec2 meets = startRound.firstWall == wall.id ? startRound.onFirst : startRound.onSecond;
        std::vector<Vec2> half = halfCornerArc(startRound, positionOf(*from), meets);
        line.insert(line.end(), half.rbegin(), half.rend());
    }
    const std::vector<Vec2> middle = curvePoints(path.curve, begin, finish);
    line.insert(line.end(), middle.begin(), middle.end());
    if (path.roundedEnd) {
        const Vec2 meets = endRound.firstWall == wall.id ? endRound.onFirst : endRound.onSecond;
        const std::vector<Vec2> half = halfCornerArc(endRound, positionOf(*to), meets);
        line.insert(line.end(), half.begin(), half.end());
    }
    dedupe(line);
    path.centreLine = std::move(line);
    return path;
}

// The outline of a wall's band.
std::vector<std::vector<Vec2>> wallBand(const Floor& floor, const Wall& wall)
{
    const WallPath path = wallPath(floor, wall);
    if (!path.curve.valid || path.centreLine.size() < 2) return {};
    const double half = wall.thickness / 2.0;
    const Point* from = findPoint(floor, wall.from);
    const Point* to = findPoint(floor, wall.to);

    BandEnd start;
    BandEnd end;
    if (path.roundedStart) {
        const CornerRounding r = cornerRounding(floor, *from);
        start.position = arcMiddle(r, positionOf(*from));
        const Vec2 radial = unit({start.position.x - r.centre.x, start.position.y - r.centre.y});
        Vec2 along = rightOf(radial);
        const Vec2 next = path.centreLine[1];
        if (dot(along, {next.x - start.position.x, next.y - start.position.y}) < 0.0) along = {-along.x, -along.y};
        start.along = along;
    } else {
        const Vec2 t = tangentAlong(path.curve, 0.0);
        start.position = plus(path.centreLine.front(), t, -half);
        start.along = t;
    }
    if (path.roundedEnd) {
        const CornerRounding r = cornerRounding(floor, *to);
        end.position = arcMiddle(r, positionOf(*to));
        const Vec2 radial = unit({end.position.x - r.centre.x, end.position.y - r.centre.y});
        Vec2 along = rightOf(radial);
        const Vec2 before = path.centreLine[path.centreLine.size() - 2];
        if (dot(along, {end.position.x - before.x, end.position.y - before.y}) < 0.0) along = {-along.x, -along.y};
        end.along = along;
    } else {
        const Vec2 t = tangentAlong(path.curve, path.curve.length);
        end.position = plus(path.centreLine.back(), t, half);
        end.along = t;
    }

    const std::vector<Vec2> outline = bandAround(path.centreLine, half, start, end);
    if (path.centreLine.size() == 2) return {outline};

    // Tight bends can fold the inner side; the union keeps only the positive loops.
    Clipper2Lib::PathD subject;
    for (const Vec2& p : outline) subject.emplace_back(p.x, p.y);
    const Clipper2Lib::PathsD cleaned = Clipper2Lib::Union(Clipper2Lib::PathsD{subject}, Clipper2Lib::FillRule::Positive, kClipperPrecision);
    std::vector<std::vector<Vec2>> result;
    for (const Clipper2Lib::PathD& path2 : cleaned) {
        std::vector<Vec2> ring;
        for (const Clipper2Lib::PointD& p : path2) ring.push_back({p.x, p.y});
        if (ring.size() >= 3 && signedArea(ring) > 0.0) result.push_back(std::move(ring));
    }
    if (result.empty()) result.push_back(outline);
    return result;
}


// The outline of a band around a centre line.
std::vector<Vec2> detail::bandAround(const std::vector<Vec2>& line, double half, const BandEnd& start, const BandEnd& end)
{
    const std::size_t n = line.size();
    std::vector<Vec2> pts(line);
    pts.front() = start.position;
    pts.back() = end.position;

    // The unit vector to the right of the wall at every point.
    std::vector<Vec2> normals(n);
    std::vector<double> scale(n, 1.0);
    normals.front() = rightOf(start.along);
    normals.back() = rightOf(end.along);
    for (std::size_t i = 1; i + 1 < n; ++i) {
        const Vec2 d1 = unit({pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y});
        const Vec2 d2 = unit({pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y});
        const Vec2 n1 = rightOf(d1);
        const Vec2 n2 = rightOf(d2);
        Vec2 mean = unit({n1.x + n2.x, n1.y + n2.y});
        double c = dot(mean, n1);
        if (c < 0.2) {
            mean = n1;
            c = 1.0;
        }
        normals[i] = mean;
        scale[i] = 1.0 / c;
    }

    std::vector<Vec2> left;
    std::vector<Vec2> right;
    for (std::size_t i = 0; i < n; ++i) {
        left.push_back(plus(pts[i], normals[i], -half * scale[i]));
        right.push_back(plus(pts[i], normals[i], half * scale[i]));
    }
    std::vector<Vec2> out = left;
    out.insert(out.end(), right.rbegin(), right.rend());
    if (signedArea(out) < 0.0) std::reverse(out.begin(), out.end());
    return out;
}

}
