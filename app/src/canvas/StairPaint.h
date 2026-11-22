#pragma once

#include "canvas/PlanCanvas.h"

#include <QTransform>

class QPainter;

namespace plynth::app {

// How a stair is drawn.
enum class StairLook {
    Normal,   // steps in Muted, footprint and arrow in Signal blue
    Selected, // everything amber
    Preview,  // the tool's live preview: blue, lighter fill
};

// Paints a stair in canvas pixels: the steps, the footprint outline and the walk line with its arrow. The
// outline is dashed amber when the stair has a problem and it is not selected.
void paintStair(QPainter& painter, const OverlayStair& stair, const QTransform& view, StairLook look);

// Paints a stair that arrives from the floor below: a dashed blue outline and a dot where it reaches this floor.
void paintArrivingStair(QPainter& painter, const OverlayStair& stair, const QTransform& view);

}
