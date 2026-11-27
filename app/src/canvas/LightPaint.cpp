#include "LightPaint.h"

#include "theme/Theme.h"

#include <QFontMetrics>
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

// Paints the glow, the line, the point handles and the label chip of one light strip.
void paintLightStrip(QPainter& painter, const QVector<QPolygonF>& pieces, const QPolygonF& points, const QString& label,
                     bool selected, bool problem)
{
    const Theme& t = Theme::instance();
    const QColor accent = selected ? t.signalAmber() : t.signalBlue();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    QColor glow = accent;
    glow.setAlphaF(t.stripGlowOpacity());
    painter.setPen(QPen(glow, t.stripGlow(), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (const QPolygonF& piece : pieces)
        painter.drawPolyline(piece);
    const QColor ink = selected || problem ? t.signalAmber() : t.text();
    painter.setPen(QPen(ink, t.stripLine(), problem ? Qt::DashLine : Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (const QPolygonF& piece : pieces)
        painter.drawPolyline(piece);

    if (selected) {
        const double half = t.handleSize() / 2.0;
        painter.setPen(QPen(t.signalAmber(), 1.5));
        painter.setBrush(t.ground());
        for (const QPointF& point : points)
            painter.drawRect(QRectF(point.x() - half, point.y() - half, half * 2.0, half * 2.0));
    }

    if (!label.isEmpty() && !pieces.isEmpty() && pieces.first().size() >= 2) {
        const QPointF a = pieces.first()[0];
        const QPointF b = pieces.first()[1];
        const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
        const QFont font = t.monoFont(t.sizeSmall());
        const QFontMetrics metrics(font);
        const int pad = t.unit();
        QRectF chip(0, 0, metrics.horizontalAdvance(label) + pad * 2.0, metrics.height() + pad);
        QPointF side(0.0, -1.0);
        if (length > 1e-6)
            side = QPointF(-(b.y() - a.y()) / length, (b.x() - a.x()) / length);
        const double away = t.stripGlow() / 2.0 + pad + chip.height() / 2.0;
        chip.moveCenter((a + b) / 2.0 + side * away);
        QColor backing = t.panel();
        backing.setAlpha(230);
        painter.setPen(QPen(accent, 1.0));
        painter.setBrush(backing);
        painter.drawRoundedRect(chip, t.radius(), t.radius());
        painter.setFont(font);
        painter.setPen(selected ? t.signalAmber() : t.text());
        painter.drawText(chip, Qt::AlignCenter, label);
    }
    painter.restore();
}

}
