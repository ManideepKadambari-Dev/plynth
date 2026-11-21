#pragma once

#include "canvas/PlanCanvas.h"

#include <QColor>
#include <QTransform>

class QPainter;

namespace plynth::app {

// How to draw one opening.
struct OpeningPaintStyle {
    QColor line;               // jambs, door leaf and arc, window lines
    bool gap = true;           // paint the footprint in gapColor first, so the wall band shows a gap
    QColor gapColor;
    bool dashedFootprint = false; // outline the footprint with a dashed line in outlineColor
    QColor outlineColor;
    QColor tint;               // fills the footprint when valid (used for previews)
};

// Draws an opening over the plan. The painter must have no transform of its own: toView maps scene units to
// canvas pixels, so line widths stay the same at every zoom.
void paintOpening(QPainter& painter, const OverlayOpening& opening, const QTransform& toView,
                  const OpeningPaintStyle& style);

}
