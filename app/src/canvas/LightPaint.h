#pragma once

#include <QColor>
#include <QPointF>

class QPainter;

namespace plynth::app {

// Paints one light marker centred on a point in canvas pixels. kind is 0 for a ceiling light (a circle with a
// cross), 1 for a wall light (a half circle on a bar) and 2 for a standing lamp (a circle with a dot). A selected
// marker gets a ring round it.
void paintLightMarker(QPainter& painter, const QPointF& centre, int kind, const QColor& ink, const QColor& fill,
                      bool selected);

}
