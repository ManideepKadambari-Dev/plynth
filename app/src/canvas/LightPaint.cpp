#include "LightPaint.h"

#include "theme/Theme.h"

#include <QPainter>
#include <QPen>

#include <cmath>

namespace plynth::app {

// Paints the marker of one light in the size the theme gives.
void paintLightMarker(QPainter& painter, const QPointF& centre, int kind, const QColor& ink, const QColor& fill,
                      bool selected)
{
    const Theme& t = Theme::instance();
    const double r = t.lightMarker() / 2.0;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(ink, t.markStroke() + 0.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(fill);
    if (kind == 1) {
        QRectF box(centre.x() - r, centre.y() - r, 2 * r, 2 * r);
        painter.drawChord(box, 0, 180 * 16);
        painter.drawLine(QPointF(centre.x() - r, centre.y()), QPointF(centre.x() + r, centre.y()));
        painter.drawLine(QPointF(centre.x(), centre.y()), QPointF(centre.x(), centre.y() - r));
    } else {
        painter.drawEllipse(centre, r, r);
        if (kind == 2) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(ink);
            painter.drawEllipse(centre, r * 0.35, r * 0.35);
        } else {
            const double d = r / std::sqrt(2.0);
            painter.drawLine(centre + QPointF(-d, -d), centre + QPointF(d, d));
            painter.drawLine(centre + QPointF(-d, d), centre + QPointF(d, -d));
        }
    }
    if (selected) {
        painter.setPen(QPen(ink, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(centre, r + 4, r + 4);
    }
    painter.restore();
}

}
