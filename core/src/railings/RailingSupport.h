#pragma once

#include "plynth/core/Railings.h"

#include <memory>
#include <utility>
#include <vector>

namespace plynth::railing {

// Shared building blocks for railing styles, all in the run's own frame (Railings.h).

// A box from u0 to u1 along the run, v0 to v1 across it and z0 to z1 up.
PartPrism box(double u0, double u1, double v0, double v1, double z0, double z1);

// A round upright of this radius standing at u on the run's line, from z0 to z1.
PartPrism roundUpright(double u, double radius, double z0, double z1);

// Every place a post stands along the run, both ends included, even where the next run puts the end post.
std::vector<double> bayEnds(const RailingRunInput& input);

// The places this run puts its own posts.
std::vector<double> postPositions(const RailingRunInput& input);

// Places spread evenly strictly between from and to, none more than gap from the next or from either end.
std::vector<double> spreadBetween(double from, double to, double gap);

// Square posts of this size, the whole railing height tall, at this run's post places.
RailingPiece squarePosts(const RailingRunInput& input, double size);

// A straight piece along the whole run, width wide and centred on the line, from z0 to z1.
RailingPiece alongRun(const RailingRunInput& input, RailingPiece::Role role, double width, double z0, double z1);

}
