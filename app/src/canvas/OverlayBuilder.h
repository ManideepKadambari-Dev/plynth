#pragma once

#include "canvas/PlanCanvas.h"

#include <plynth/core/Plan.h>

namespace plynth::app {

// Builds what the canvas draws over the plan image for the first floor of a plan, in image pixels. Walls
// and rooms come from the outlines core makes (wallBand, roomShape), so curved walls and rounded corners
// look the same here as in every other program. Nothing without a scale.
OverlayData buildOverlay(const plynth::Plan& plan);

// Works out where an opening is drawn on the plan, in image pixels, from core's openingPlacement. Not valid
// when the opening has no place (its wall is missing).
OverlayOpening makeOverlayOpening(const plynth::Floor& floor, const plynth::Opening& opening, double ppm);

// Adds every opening of the floor to the overlay, marking the ones that no longer fit their wall.
void addOverlayOpenings(OverlayData& overlay, const plynth::Floor& floor, double ppm);

}
