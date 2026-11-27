#include "RoofCuts.h"

#include "../PlanRingClean.h"
#include "../RingClean.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace plynth {

namespace {

// Below this share of its length the up part of a face's normal counts as none: the face stands upright.
constexpr double kUprightShare = 1e-6;
// Pieces smaller than this, in square metres, are dropped.
constexpr double kTinyArea = 1e-9;

// How a face's plane maps to flat points and back. A face that does not stand upright is seen from above: flat
// points are (x, y) and z follows the plane. An upright face is seen from the front: flat points are (s, z),
// s along the face from origin.
struct FacePlane {
    bool upright = false;
    Vec3 origin;
    double slopeX = 0.0; // dz/dx on the plane, for a face that does not stand upright
    double slopeY = 0.0;
    Vec2 along;          // the unit direction of s on the plan, for an upright face

    // The flat point of a point on the plane.
    Vec2 flat(const Vec3& p) const
    {
        if (!upright) return {p.x, p.y};
        return {(p.x - origin.x) * along.x + (p.y - origin.y) * along.y, p.z};
    }

    // The point on the plane of a flat point.
    Vec3 lift(Vec2 p) const
    {
        if (!upright) return {p.x, p.y, origin.z + slopeX * (p.x - origin.x) + slopeY * (p.y - origin.y)};
        return {origin.x + p.x * along.x, origin.y + p.x * along.y, p.y};
    }
};

// The plane of a face from its outline. Empty when the outline has no area.
std::optional<FacePlane> planeOf(const std::vector<Vec3>& ring)
{
    if (ring.size() < 3) return std::nullopt;
    double nx = 0.0;
    double ny = 0.0;
    double nz = 0.0;
    Vec3 mean;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec3& a = ring[i];
        const Vec3& b = ring[(i + 1) % ring.size()];
        nx += (a.y - b.y) * (a.z + b.z);
        ny += (a.z - b.z) * (a.x + b.x);
        nz += (a.x - b.x) * (a.y + b.y);
        mean.x += a.x;
        mean.y += a.y;
        mean.z += a.z;
    }
    const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (length < 1e-12) return std::nullopt;
    const double count = static_cast<double>(ring.size());
    FacePlane plane;
    plane.origin = {mean.x / count, mean.y / count, mean.z / count};
    if (std::fabs(nz) > kUprightShare * length) {
        plane.slopeX = -nx / nz;
        plane.slopeY = -ny / nz;
        return plane;
    }
    const double flatLength = std::hypot(nx, ny);
    plane.upright = true;
    plane.along = {-ny / flatLength, nx / flatLength};
    return plane;
}

// A ring as a Clipper path.
Clipper2Lib::PathD pathOf(const std::vector<Vec2>& ring)
{
    Clipper2Lib::PathD path;
    path.reserve(ring.size());
    for (const Vec2& p : ring) path.emplace_back(p.x, p.y);
    return path;
}

// The ring turned so its area has the wanted sign.
std::vector<Vec2> turned(std::vector<Vec2> ring, bool positive)
{
    if ((ringAreaOf(ring) > 0.0) != positive) std::reverse(ring.begin(), ring.end());
    return ring;
}

// The total area of plain paths, holes counted negative.
double areaOf(const Clipper2Lib::PathsD& paths)
{
    double total = 0.0;
    for (const Clipper2Lib::PathD& path : paths) total += ringAreaOf(ringOfPath(path));
    return total;
}

// The flat region the cutter covers on a plane, as plain paths.
Clipper2Lib::PathsD cutRegion(const FacePlane& plane, const PartPrism& cutter, double reach)
{
    Clipper2Lib::PathsD out;
    if (plane.upright) {
        // The cutter's plan region along the face's line, as spans of s, each raised from bottom to top.
        std::vector<double> crossings;
        const auto collect = [&](const std::vector<Vec2>& ring) {
            for (std::size_t i = 0; i < ring.size(); ++i) {
                const Vec2 a = ring[i];
                const Vec2 b = ring[(i + 1) % ring.size()];
                const double da = (a.x - plane.origin.x) * plane.along.y - (a.y - plane.origin.y) * plane.along.x;
                const double db = (b.x - plane.origin.x) * plane.along.y - (b.y - plane.origin.y) * plane.along.x;
                if ((da > 0.0) == (db > 0.0)) continue;
                const double t = da / (da - db);
                const Vec2 p{a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)};
                crossings.push_back((p.x - plane.origin.x) * plane.along.x + (p.y - plane.origin.y) * plane.along.y);
            }
        };
        collect(cutter.outline);
        for (const std::vector<Vec2>& hole : cutter.holes) collect(hole);
        std::sort(crossings.begin(), crossings.end());
        for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
            const double s0 = crossings[i];
            const double s1 = crossings[i + 1];
            if (s1 - s0 < detail::kRingClose) continue;
            out.push_back(Clipper2Lib::PathD{Clipper2Lib::PointD(s0, cutter.bottom), Clipper2Lib::PointD(s1, cutter.bottom),
                                             Clipper2Lib::PointD(s1, cutter.top), Clipper2Lib::PointD(s0, cutter.top)});
        }
        return out;
    }

    Clipper2Lib::PathsD region{pathOf(turned(cutter.outline, true))};
    for (const std::vector<Vec2>& hole : cutter.holes) region.push_back(pathOf(turned(hole, false)));
    const double slope = std::hypot(plane.slopeX, plane.slopeY);
    if (slope < 1e-12) {
        const bool within = plane.origin.z >= cutter.bottom - detail::kRingClose && plane.origin.z <= cutter.top + detail::kRingClose;
        return within ? clipped(Clipper2Lib::ClipType::Union, region, {}) : out;
    }
    // The band of the plane between the cutter's bottom and top, as a long strip across the slope.
    const Vec2 uphill{plane.slopeX / slope, plane.slopeY / slope};
    const Vec2 across{-uphill.y, uphill.x};
    const double low = std::clamp((cutter.bottom - plane.origin.z) / slope, -reach, reach);
    const double high = std::clamp((cutter.top - plane.origin.z) / slope, -reach, reach);
    if (high - low < detail::kRingClose) return out;
    const auto at = [&](double t, double u) {
        return Clipper2Lib::PointD(plane.origin.x + t * uphill.x + u * across.x, plane.origin.y + t * uphill.y + u * across.y);
    };
    const Clipper2Lib::PathsD band{Clipper2Lib::PathD{at(low, -reach), at(high, -reach), at(high, reach), at(low, reach)}};
    return clipped(Clipper2Lib::ClipType::Intersection, region, band);
}

// How far the long strip of a cut must reach: past every point of the face and the cutter.
double reachOf(const RoofFace& face, const PartPrism& cutter, const Vec3& origin)
{
    double most = 0.0;
    const auto take = [&](double x, double y) {
        most = (std::max)(most, (std::max)(std::fabs(x - origin.x), std::fabs(y - origin.y)));
    };
    for (const Vec3& p : face.outline) take(p.x, p.y);
    for (const Vec2& p : cutter.outline) take(p.x, p.y);
    return 2.0 * most + 10.0;
}

// The pieces of one face that are left once the cutter is taken out, or the face itself when it misses.
std::vector<RoofFace> cutFace(const RoofFace& face, const PartPrism& cutter)
{
    const std::optional<FacePlane> plane = planeOf(face.outline);
    if (!plane || cutter.outline.size() < 3 || cutter.top <= cutter.bottom) return {face};

    std::vector<Vec2> outline;
    for (const Vec3& p : face.outline) outline.push_back(plane->flat(p));
    const bool reversed = ringAreaOf(outline) < 0.0;
    Clipper2Lib::PathsD subject{pathOf(turned(outline, true))};
    for (const std::vector<Vec3>& hole : face.holes) {
        std::vector<Vec2> ring;
        for (const Vec3& p : hole) ring.push_back(plane->flat(p));
        subject.push_back(pathOf(turned(ring, false)));
    }

    const Clipper2Lib::PathsD region = cutRegion(*plane, cutter, reachOf(face, cutter, plane->origin));
    if (region.empty()) return {face};
    if (areaOf(clipped(Clipper2Lib::ClipType::Intersection, subject, region)) < kTinyArea) return {face};

    std::vector<RoofFace> out;
    for (const RingPiece& piece : piecesByTurning(clipped(Clipper2Lib::ClipType::Difference, subject, region))) {
        std::vector<Vec2> kept = detail::withoutRepeats(piece.outline);
        if (kept.size() < 3) continue;
        double size = ringAreaOf(kept);
        RoofFace part;
        part.part = face.part;
        if (reversed) std::reverse(kept.begin(), kept.end());
        for (const Vec2& p : kept) part.outline.push_back(plane->lift(p));
        for (const std::vector<Vec2>& points : piece.holes) {
            std::vector<Vec2> hole = detail::withoutRepeats(points);
            if (hole.size() < 3) continue;
            size += ringAreaOf(hole);
            if (reversed) std::reverse(hole.begin(), hole.end());
            std::vector<Vec3> lifted;
            for (const Vec2& p : hole) lifted.push_back(plane->lift(p));
            part.holes.push_back(std::move(lifted));
        }
        if (size < kTinyArea) continue;
        out.push_back(std::move(part));
    }
    return out;
}

}

// Cuts the prism out of every face it passes through.
void cutRoofFaces(std::vector<RoofFace>& faces, const PartPrism& cutter)
{
    std::vector<RoofFace> out;
    for (const RoofFace& face : faces) {
        for (RoofFace& piece : cutFace(face, cutter)) out.push_back(std::move(piece));
    }
    faces = std::move(out);
}

}
