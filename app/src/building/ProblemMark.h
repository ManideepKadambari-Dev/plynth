#pragma once

#include <QColor>
#include <QPainter>
#include <QPolygonF>
#include <QRectF>

namespace plynth::app {

// Draws the amber warning triangle of a build problem inside a rectangle: filled for an error, an outline for a
// warning.
inline void paintProblemMark(QPainter& p, const QRectF& box, bool error, const QColor& amber, qreal lineWidth)
{
    const QRectF r = box.adjusted(lineWidth, lineWidth, -lineWidth, -lineWidth);
    QPolygonF triangle;
    triangle << QPointF(r.center().x(), r.top()) << QPointF(r.right(), r.bottom()) << QPointF(r.left(), r.bottom());
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(amber, lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(error ? QBrush(amber) : QBrush(Qt::NoBrush));
    p.drawPolygon(triangle);
    p.restore();
}

}
