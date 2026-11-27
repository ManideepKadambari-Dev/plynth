#pragma once

#include "RoofParts.h"

#include <optional>
#include <utility>
#include <vector>

// The straight skeleton of an area, and small helpers the hip and pyramid roofs share. Everything is in world
// coordinates, metres. The skeleton is what a roof of equal pitch on every side looks like seen from above: every
// edge of the area gets one face, the part of the area its edge reaches first when all edges move inward at the
// same speed. A face lies in the plane through its edge at that pitch, so neighbouring faces meet in hips, valleys
// and ridges.
namespace plynth::roofskeleton {

// The face of one edge of the area.
struct SkeletonFace {
    Vec2 from; // the edge's ends, in the turning of its ring, so the area lies on the left
    Vec2 to;
    std::vector<std::vector<Vec2>> parts; // outlines, anticlockwise; the first starts with from and to
};

// A straight skeleton: one face per edge, the outline's edges first, then those of each hole in order, and the
// lines between faces.
struct Skeleton {
    std::vector<SkeletonFace> faces;
    std::vector<std::pair<Vec2, Vec2>> arcs;
};

// The straight skeleton of an area. The outline must turn left and the holes right, without repeated points.
// Empty when the area is broken, or when the faces worked out do not add up to the area.
std::optional<Skeleton> straightSkeleton(const WorldPolygon& area);

// The ring without points that lie within 1e-3 m of the line between their neighbours.
std::vector<Vec2> withoutStraightPoints(std::vector<Vec2> ring);

// Says whether a ring turns left at every point.
bool convexRing(const std::vector<Vec2>& ring);

// Moves the inner points of the pieces that come within 1e-4 m of each other, along a chain, to their mean, so
// faces that meet at one apex share it exactly. A group spread over more than 2e-4 m is left as it is. Points on
// the eaves never move. Pieces left with nothing are dropped.
void weldInnerPoints(std::vector<roofparts::Piece>& pieces, const std::vector<WorldPolygon>& eaves);

}
