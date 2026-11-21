#pragma once

#include "plynth/core/detection/Detection.h"
#include "plynth/core/detection/GrayImage.h"

#include <stop_token>

namespace plynth {

// Finds straight horizontal and vertical walls in a plan image with classic image processing. This is the
// work behind the "orthogonal" detector. It keeps no state, so it is safe to call from several threads.
// Fills points, walls and openings, the image size and status; a stop asked for gives cancelled.
DetectionResult detectOrthogonal(const GrayImage& image, double pixelsPerMetre, const DetectionParams& params,
                                 std::stop_token stop);

}
