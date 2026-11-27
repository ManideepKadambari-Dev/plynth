#pragma once

#include "BuildParts.h"
#include "Plan.h"
#include "Status.h"

#include <vector>

namespace plynth {

// Linear and cove lights (issue 19, plan format 8): the part maker "light_strips" (order 80, kind "light",
// group "surfaces"), core/src/parts/makers/LightStripParts.cpp.
//
// The kinds: cove runs along a ceiling edge and shines up, soffit runs under a cabinet, a shelf or an overhang
// and shines down, edge runs along a wall or a slab edge at a height and shines outward (to the right of the
// line on the plan), step lights the nose of every step of a stair.
//
// Every straight piece of a strip's line gives a StripLight (BuildParts.h): an area light kStripWidth wide
// along it, with power = strip power per metre times the piece's length. For kind step the pieces are the noses
// of the stair's steps (landings and a spiral's column have none), kStepStripInset in from each side of the
// stair and kStepStripDrop below the tread, on the riser face, shining down and forward. And one part
// <floor>_<strip>_Strip: a slim housing kStripWidth by kStripDepth along every piece in the neutral Light_Strip
// slot, open on the side the light shines from, with the light kStripGap out from that side, no collision and
// no bevel. A cove housing hangs below z, a soffit housing sits above it, an edge housing lies behind the line
// (to its left) centred on z, a step housing stands out from the riser. The lights go into the gameplay data
// file's lights with kind "strip", their two ends, facing, width and power (GameplayData.h), so an engine can
// rebuild them as its own lights. The FBX carries them as lights, which engines import as point or area
// lights depending on the engine.
// Problems: light_strip_stair_missing (error: kind step and the stair is gone; not built). A step strip whose
// stair cannot be built gives nothing; the stair has its own problem.

inline constexpr double kStripWidth = 0.02;  // metres
inline constexpr double kStripDepth = 0.015;
inline constexpr double kStepStripInset = 0.03;
inline constexpr double kStepStripDrop = 0.02;
inline constexpr double kStripGap = 0.001;      // between a housing's open side and its light
inline constexpr double kMinStripLength = 0.1;  // the whole line of a strip that is not kind step
inline constexpr double kMinStripPiece = 0.01;  // between two points in a row

// Checks the values a light strip command would set, with the rules of checkPlan and these: a strip that is
// not kind step has no stair, at least two points, no two in a row closer than kMinStripPiece and a line at
// least kMinStripLength long; a step strip has no points and names a stair of the floor (NotFound otherwise).
// InvalidValue with a plain sentence.
Status checkLightStrip(const LightStrip& strip, const Floor& floor);

// The height a new strip of a kind starts at: the ceiling height minus 0.05 for cove, the floor's ceiling height
// for soffit, 0 for edge and step.
double defaultStripHeight(LightStripKind kind, double ceilingHeight);

}
