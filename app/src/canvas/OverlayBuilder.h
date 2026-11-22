#pragma once

#include "canvas/PlanCanvas.h"

#include <plynth/core/Addons.h>
#include <plynth/core/Generators.h>
#include <plynth/core/Plan.h>
#include <plynth/core/catalog/AssetCatalog.h>

#include <optional>

namespace plynth::app {

// Builds what the canvas draws over the plan image for the first floor of a plan, in image pixels. Walls
// and rooms come from the outlines core makes (wallBand, roomShape), so curved walls and rounded corners
// look the same here as in every other program. Items come from core's itemFootprint and itemProblems; with a
// catalog, items whose asset is gone or has a new size are marked, and without one they are not. The same
// goes for generator items with a generator library and the add-on cache: a generator that is gone, one that
// needs an add-on that is off, or one whose values no longer fit is marked. Nothing without a scale.
OverlayData buildOverlay(const plynth::Plan& plan, const plynth::AssetCatalog* catalog = nullptr,
                         const plynth::GeneratorLibrary* generators = nullptr,
                         const std::optional<plynth::AddonCache>* addons = nullptr);

// Works out where an opening is drawn on the plan, in image pixels, from core's openingPlacement. Not valid
// when the opening has no place (its wall is missing).
OverlayOpening makeOverlayOpening(const plynth::Floor& floor, const plynth::Opening& opening, double ppm);

// Adds every opening of the floor to the overlay, marking the ones that no longer fit their wall.
void addOverlayOpenings(OverlayData& overlay, const plynth::Floor& floor, double ppm);

}
