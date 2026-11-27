#pragma once

#include <QColor>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QVector>

class QPainter;

namespace plynth::app {

// Paints one light marker centred on a point in canvas pixels. kind is 0 for a ceiling light (a circle with a
// cross), 1 for a wall light (a half circle on a bar) and 2 for a standing lamp (a circle with a dot). A selected
// marker gets a ring round it.
void paintLightMarker(QPainter& painter, const QPointF& centre, int kind, const QColor& ink, const QColor& fill,
                      bool selected);

// Paints one light strip in canvas pixels: a soft glow round every piece with a thin bright line along it, amber
// when selected, dashed amber when it has a problem. A selected strip shows a handle on each of its points. The
// label goes in a small chip beside the middle of the first piece.
void paintLightStrip(QPainter& painter, const QVector<QPolygonF>& pieces, const QPolygonF& points, const QString& label,
                     bool selected, bool problem);

}
