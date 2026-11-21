#include "OpeningPaint.h"

#include <QPainter>
#include <QPen>

namespace plynth::app {

// Draws an opening over the plan: the gap in the wall, the jamb lines, and for a door its leaf and swing arc,
// for a window its two thin lines.
void paintOpening(QPainter& painter, const OverlayOpening& opening, const QTransform& toView,
                  const OpeningPaintStyle& style)
{
    if (!opening.valid)
        return;
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPolygonF footprint = toView.map(opening.footprint);
    if (style.gap) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(style.gapColor);
        painter.drawPolygon(footprint);
    }
    if (style.tint.isValid() && style.tint.alpha() > 0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(style.tint);
        painter.drawPolygon(footprint);
    }
    painter.setBrush(Qt::NoBrush);
    const QPen line(style.line, 1.5);
    painter.setPen(line);
    painter.drawLine(toView.map(opening.startJamb));
    painter.drawLine(toView.map(opening.endJamb));
    if (opening.type == 1) {
        painter.drawLine(toView.map(opening.leaf));
        painter.drawPolyline(toView.map(opening.arc));
    } else if (opening.type == 2) {
        QPen thin = line;
        thin.setWidthF(1.0);
        painter.setPen(thin);
        painter.drawPolyline(toView.map(opening.glassA));
        painter.drawPolyline(toView.map(opening.glassB));
    }
    if (style.dashedFootprint) {
        QPen dashed(style.outlineColor, 1.5);
        dashed.setDashPattern({4.0, 3.0});
        painter.setPen(dashed);
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(footprint);
    }
}

}
