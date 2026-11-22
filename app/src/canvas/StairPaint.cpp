#include "StairPaint.h"

#include "theme/Theme.h"

#include <QPainter>
#include <QPolygonF>

#include <cmath>

namespace plynth::app {

namespace {

// A polygon in scene units as a polygon in canvas pixels.
QPolygonF mapped(const QPolygonF& polygon, const QTransform& view)
{
    return view.map(polygon);
}

// Draws the arrow head at the end of a walk line.
void paintArrowHead(QPainter& painter, const QPolygonF& line, const QColor& color)
{
    if (line.size() < 2)
        return;
    const QPointF tip = line.last();
    QPointF back = line[line.size() - 2];
    for (int i = static_cast<int>(line.size()) - 2; i >= 0; --i) {
        back = line[i];
        if (std::hypot(tip.x() - back.x(), tip.y() - back.y()) > 1.0)
            break;
    }
    const double length = std::hypot(tip.x() - back.x(), tip.y() - back.y());
    if (length < 1.0)
        return;
    const QPointF direction = (tip - back) / length;
    const QPointF side(-direction.y(), direction.x());
    const double size = Theme::instance().unit() * 3.0;
    QPolygonF head;
    head << tip << tip - direction * size + side * (size / 2.0) << tip - direction * size - side * (size / 2.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(head);
}

}

// Paints the steps, the footprint and the walk line of a stair.
void paintStair(QPainter& painter, const OverlayStair& stair, const QTransform& view, StairLook look)
{
    const Theme& t = Theme::instance();
    const bool selected = look == StairLook::Selected;
    const bool preview = look == StairLook::Preview;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor fill = preview ? t.signalBlue() : t.raised();
    fill.setAlpha(preview ? t.roomFillAlpha() * 2 : t.itemFillAlpha());
    const QColor stepLine = selected ? t.signalAmber() : preview ? t.signalBlue() : t.muted();
    painter.setPen(QPen(stepLine, 1.0));
    painter.setBrush(fill);
    for (const OverlayStairStep& step : stair.steps) {
        if (step.part == 2) {
            painter.setBrush(stepLine);
            painter.drawPolygon(mapped(step.outline, view));
            painter.setBrush(fill);
        } else {
            painter.drawPolygon(mapped(step.outline, view));
        }
    }

    const QColor accent = selected ? t.signalAmber() : t.signalBlue();
    QPen outline(accent, selected ? 2.0 : 1.5);
    if (stair.problem && !selected)
        outline.setDashPattern({4.0, 3.0});
    painter.setPen(outline);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(mapped(stair.footprint, view));

    const QPolygonF walk = mapped(stair.walkLine, view);
    if (walk.size() >= 2) {
        painter.setPen(QPen(accent, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolyline(walk);
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        const double radius = t.pointDot() / 2.0 - 1.0;
        painter.drawEllipse(walk.first(), radius, radius);
        paintArrowHead(painter, walk, accent);
    }
    painter.restore();
}

// Paints the locked outline and the arrival dot of a stair that comes up from the floor below.
void paintArrivingStair(QPainter& painter, const OverlayStair& stair, const QTransform& view)
{
    const Theme& t = Theme::instance();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor fill = t.signalBlue();
    fill.setAlpha(t.roomFillAlpha() / 2);
    QPen outline(t.signalBlue(), 1.5);
    outline.setDashPattern({4.0, 3.0});
    painter.setPen(outline);
    painter.setBrush(fill);
    painter.drawPolygon(mapped(stair.footprint, view));
    const QPointF at = view.map(stair.arrival);
    const double radius = t.pointDot() / 2.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(t.signalBlue());
    painter.drawEllipse(at, radius, radius);
    painter.restore();
}

}
