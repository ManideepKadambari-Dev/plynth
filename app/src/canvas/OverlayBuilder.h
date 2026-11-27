#pragma once

#include "canvas/PlanCanvas.h"

#include <plynth/core/Addons.h>
#include <plynth/core/Generators.h>
#include <plynth/core/Plan.h>
#include <plynth/core/Roofs.h>
#include <plynth/core/Stairs.h>
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

// The same for the floor with this id. Also gives the floor's image offset and the faint outline of the walls
// of the floor below. Scene units are plan metres times the scale, so the floor's image sits at its offset.
OverlayData buildOverlay(const plynth::Plan& plan, const std::string& floorId, const plynth::AssetCatalog* catalog,
                         const plynth::GeneratorLibrary* generators = nullptr,
                         const std::optional<plynth::AddonCache>* addons = nullptr);

// A stair layout from core as what the canvas draws, in image pixels. Problems are the stair's problem
// sentences, one per line.
OverlayStair makeOverlayStair(const plynth::StairLayout& layout, double ppm);

// A roof section from core as what the canvas draws, in image pixels. Its id is "<floor id>:<index>".
OverlayRoof makeOverlayRoof(const plynth::RoofArea& roof, double ppm);

// Works out where an opening is drawn on the plan, in image pixels, from core's openingPlacement. Not valid
// when the opening has no place (its wall is missing).
OverlayOpening makeOverlayOpening(const plynth::Floor& floor, const plynth::Opening& opening, double ppm);

// Adds every opening of the floor to the overlay, marking the ones that no longer fit their wall.
void addOverlayOpenings(OverlayData& overlay, const plynth::Floor& floor, double ppm);

// Adds every canopy of the floor to the overlay: its outline as drawn, its id and the generator items standing
// inside it as its posts.
void addOverlayCanopies(OverlayData& overlay, const plynth::Floor& floor, double ppm);

}
