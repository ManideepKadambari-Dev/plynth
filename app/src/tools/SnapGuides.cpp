#include "SnapGuides.h"

#include "canvas/PlanCanvas.h"
#include "theme/Theme.h"

#include <QPainter>
#include <QPen>

namespace plynth::app {

// Draws a dashed amber line from each point the position lined up with to the position, then the snap mark: a
// ring on a point, a cross on a wall, a plus elsewhere.
void paintSnap(QPainter& painter, PlanCanvas& canvas, double pixelsPerMetre, const plynth::SnapResult& snap)
{
    const Theme& t = Theme::instance();
    const auto toView = [&](plynth::Vec2 v) { return canvas.sceneToView(QPointF(v.x * pixelsPerMetre, v.y * pixelsPerMetre)); };
    const QPointF at = toView(snap.position);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);

    QPen guide(t.signalAmber(), 1);
    guide.setDashPattern({static_cast<qreal>(t.proposalDash()), static_cast<qreal>(t.proposalDash())});
    painter.setPen(guide);
    for (const std::optional<plynth::Vec2>& from : {snap.alignedX, snap.alignedY}) {
        if (from.has_value())
            painter.drawLine(toView(*from), at);
    }

    painter.setPen(QPen(t.signalAmber(), 2));
    const double r = t.pointDot() / 2.0;
    switch (snap.kind) {
    case plynth::SnapKind::Point:
        painter.drawEllipse(at, r + 4, r + 4);
        break;
    case plynth::SnapKind::Wall:
        painter.drawLine(at + QPointF(-r - 2, -r - 2), at + QPointF(r + 2, r + 2));
        painter.drawLine(at + QPointF(-r - 2, r + 2), at + QPointF(r + 2, -r - 2));
        painter.drawEllipse(at, 2, 2);
        break;
    case plynth::SnapKind::Align:
    case plynth::SnapKind::Grid:
    case plynth::SnapKind::None:
        painter.drawLine(at + QPointF(-r, 0), at + QPointF(r, 0));
        painter.drawLine(at + QPointF(0, -r), at + QPointF(0, r));
        break;
    }
}

}
