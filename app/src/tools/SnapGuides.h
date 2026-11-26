#pragma once

#include <plynth/core/Snapping.h>

class QPainter;

namespace plynth::app {

class PlanCanvas;

// Draws the thin guide lines of a snap that lined up with other points, and the mark of what it snapped to.
void paintSnap(QPainter& painter, PlanCanvas& canvas, double pixelsPerMetre, const plynth::SnapResult& snap);

}
