#pragma once

#include "plynth/core/BuildGeometry.h"
#include "plynth/core/BuildParts.h"

#include <vector>

namespace plynth {

// Cuts the cutter prism out of every face it passes through (a RoofCut, BuildParts.h). A face it misses stays as
// it was; a face it cuts may become several faces, each on the same plane with the same part and turning the
// same way; a face wholly inside it goes.
void cutRoofFaces(std::vector<RoofFace>& faces, const PartPrism& cutter);

}
