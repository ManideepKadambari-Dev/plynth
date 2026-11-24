#pragma once

#include "plynth/core/Roofs.h"

#include <functional>
#include <optional>
#include <vector>

// Shared building blocks for roof types. They are written in Roofs.cpp, so a roof type file needs no polygon
// library of its own. Everything is in world coordinates, metres.
namespace plynth::roofparts {

// A plane z = a * x + b * y + c.
struct Plane {
    double a = 0.0;
    double b = 0.0;
    double c = 0.0;

    double at(Vec2 p) const { return a * p.x + b * p.y + c; }
};

// A line along which a roof's height changes direction, such as a ridge.
struct BreakLine {
    Vec2 point;
    Vec2 direction;
};

// A part of the eaves outline that lies on one plane.
struct Piece {
    std::vector<WorldPolygon> area;
    Plane top;
};

// The part of the polygons on the side of the line through point where (p - point) . normal >= 0.
std::vector<WorldPolygon> keepHalf(const std::vector<WorldPolygon>& polygons, Vec2 point, Vec2 normal);

// The polygons cleaned of rounding noise as the finished eaves are: once by width, then piece by piece.
std::vector<WorldPolygon> tidied(const std::vector<WorldPolygon>& polygons);

// The polygons shrunk by distance on every side, holes grown.
std::vector<WorldPolygon> inset(const std::vector<WorldPolygon>& polygons, double distance);

// What is in the first set and not in the second.
std::vector<WorldPolygon> without(const std::vector<WorldPolygon>& polygons, const std::vector<WorldPolygon>& cut);

// Horizontal faces at z, one per polygon. Up faces look up, others look down.
std::vector<RoofFace> flatFaces(const std::vector<WorldPolygon>& polygons, double z, bool up, RoofPart part);

// Vertical faces on every ring of the polygons from bottom to top, looking away from the polygons' material.
std::vector<RoofFace> ringWalls(const std::vector<WorldPolygon>& polygons, double bottom, double top, RoofPart part);

// One face from a polygon with every point lifted to a plane and lowered by drop. A down face turns every ring.
RoofFace planeFace(const WorldPolygon& polygon, const Plane& plane, double drop, bool up, RoofPart part);

// A vertical face on the edge from p to q with a bottom and a top height at each end, looking to the right of
// the direction p to q.
RoofFace wallFace(Vec2 p, Vec2 q, double pBottom, double qBottom, double pTop, double qTop, RoofPart part);

// The vertical gable fill on the edge from a to b, where the underside stands ga and gb above base at the two
// ends (linear between). Empty when it nowhere stands above base.
std::optional<RoofFace> gableFill(Vec2 a, Vec2 b, double base, double ga, double gb);

// Covering, underside, edge and gable faces of a roof made of planes. height gives the top surface at a plan
// position; the roof is kRoofThickness thick below it. The pieces cover input.eaves, the gable fills stand on
// input.section from input.base up to the underside, and the breaks are where the height changes direction.
std::vector<RoofFace> pitchedFaces(const RoofInput& input, const std::vector<Piece>& pieces,
                                   const std::vector<BreakLine>& breaks, const std::function<double(Vec2)>& height);

// The unit normal of the direction turned a quarter to the left on the plan seen from above.
inline Vec2 leftOf(Vec2 d) { return {-d.y, d.x}; }

}
