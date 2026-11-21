#pragma once

#include "Geometry.h"
#include "Plan.h"

#include <vector>

namespace plynth {

// The shape of walls on the plan: straight lines or true circular arcs (from bulge), and rounded corners
// (from corner radius). Everything that draws, snaps, picks, finds rooms or measures areas goes through these
// functions, so the app, the CLI and the Blender build see the same walls. Lengths in metres, angles in
// radians, positions in plan coordinates (x right, y down).

// A bulge smaller than this, either sign, counts as straight. SetWallBulgeCommand stores 0 for it.
inline constexpr double kMinBulge = 0.001;

// The largest bulge is this times the straight distance between the wall's points: 0.5 is a half circle.
inline constexpr double kMaxBulgeRatio = 0.5;

// The true centre line of a wall, before corners are rounded.
// For a curved wall, the place at distance s along it is centre + radius * (cos a, sin a) with
// a = startAngle + sweep * s / length, so sweep is negative or positive depending on the way it bends.
struct WallCurve {
    bool valid = false;  // false when a point is missing or the two points are less than 1e-9 apart
    Vec2 from;
    Vec2 to;
    double chord = 0.0;  // straight distance between from and to
    bool curved = false; // true when |bulge| is at least kMinBulge
    double bulge = 0.0;  // as stored on the wall, 0 when straight
    Vec2 centre;         // curved only
    double radius = 0.0; // curved only: (chord^2 / 4 + bulge^2) / (2 |bulge|)
    double startAngle = 0.0; // curved only: atan2(from.y - centre.y, from.x - centre.x)
    double sweep = 0.0;  // curved only: signed, 4 * atan(2 |bulge| / chord) in size
    double length = 0.0; // along the curve
};

// The curve between two positions with a bulge, following the bulge rule in Plan.h.
WallCurve curveBetween(Vec2 from, Vec2 to, double bulge);

// The true centre line of a wall of a floor. Not valid when one of its points is missing.
WallCurve wallCurve(const Floor& floor, const Wall& wall);

// The place at a distance along the curve from its from end, clamped to 0 to length.
Vec2 pointAlong(const WallCurve& curve, double offset);

// The unit direction of the curve at a distance along it, pointing toward its to end.
Vec2 tangentAlong(const WallCurve& curve, double offset);

// The place on a curve closest to a position.
struct CurveProjection {
    Vec2 position;
    double offset = 0.0;   // distance along the curve from its from end, 0 to length
    double distance = 0.0; // from the position to the curve
    WallSide side = WallSide::Right; // the side of the curve the position is on
};

// Finds the place on the curve closest to a position. For a curve that is not valid, offset 0 at from.
CurveProjection projectOntoCurve(Vec2 position, const WallCurve& curve);

// The curve between two distances along it as a polyline, cut by arcSegmentCount for the part's own sweep.
// Starts exactly at pointAlong(fromOffset) and ends exactly at pointAlong(toOffset). Two points when straight.
std::vector<Vec2> curvePoints(const WallCurve& curve, double fromOffset, double toOffset);

// The bulge that puts the middle of the arc between from and to nearest a position, for dragging a wall's
// middle: the distance of the position from the straight line along the right side direction, limited to
// plus or minus kMaxBulgeRatio times the straight distance. 0 when from and to are the same place.
double bulgeFromPosition(Vec2 from, Vec2 to, Vec2 position);

// The side of a wall a position is on, for choosing the face the wall view shows.
WallSide wallSideOf(const Floor& floor, const Wall& wall, Vec2 position);

// The height of a wall: its own height when set, otherwise the floor's ceiling height.
double wallHeight(const Floor& floor, const Wall& wall);

// How a corner point is rounded.
// A point is rounded when its corner radius is above zero and it joins exactly two walls of the floor, both
// straight, that meet at an angle between 1 and 179 degrees. The arc touches both walls' centre lines. Where
// it meets each wall is trim = radius / tan(angle / 2) from the point. The radius used is the one asked for,
// made smaller when needed so trim is at most half of each of the two walls' lengths.
struct CornerRounding {
    bool rounded = false;
    double radius = 0.0;  // the radius used, 0 when not rounded
    double trim = 0.0;    // distance along each wall from the point to where the arc meets it
    Id firstWall;         // the two walls, in the order of the floor's wall list
    Id secondWall;
    Vec2 centre;
    Vec2 onFirst;         // where the arc meets the first wall
    Vec2 onSecond;        // where the arc meets the second wall
    std::vector<Vec2> arc; // from onFirst to onSecond, cut by arcSegmentCount
};

// Works out how a point is rounded. Not rounded for a point that is not on the floor.
CornerRounding cornerRounding(const Floor& floor, const Point& point);

// A wall as it is drawn and built: its true curve, shortened at rounded corners, plus its half of each
// corner arc.
struct WallPath {
    WallCurve curve;
    double startTrim = 0.0; // distance cut from the from end by a rounded corner
    double endTrim = 0.0;   // distance cut from the to end by a rounded corner
    bool roundedStart = false;
    bool roundedEnd = false;
    // The drawn centre line from the from end to the to end: half of the corner arc at a rounded from end
    // (from the arc's middle), the curve between the trims, then half of the corner arc at a rounded to end
    // (up to the arc's middle). No two points in a row are the same.
    std::vector<Vec2> centreLine;
};

// Works out the path of a wall of a floor. Empty centreLine when the wall's curve is not valid.
WallPath wallPath(const Floor& floor, const Wall& wall);

// The outline of a wall's band as drawn on the plan and used for picking and floor areas: centreLine offset by
// half the thickness on each side. A plain end is extended by half the thickness along its direction, as in
// milestone 3, so straight walls meeting at a sharp corner overlap there. A rounded end is cut straight
// across at the middle of the corner arc, so the two walls' bands meet without overlapping. Each outline has
// a positive signedArea. Usually one outline; empty when the wall's curve is not valid.
std::vector<std::vector<Vec2>> wallBand(const Floor& floor, const Wall& wall);

}
