#include "ProjectThumbnail.h"

#include "canvas/OverlayBuilder.h"
#include "theme/Theme.h"

#include <QPainter>

namespace plynth::app {

// Scales the image down and paints the rooms and the walls of the first floor over it, in theme colors.
QImage renderProjectThumbnail(const plynth::Plan& plan, const QImage& planImage, int maxSide)
{
    if (planImage.isNull() || maxSide < 1)
        return QImage();
    const Theme& t = Theme::instance();
    const double factor = double(maxSide) / std::max(planImage.width(), planImage.height());
    QImage out(std::max(1, int(planImage.width() * factor)), std::max(1, int(planImage.height() * factor)),
               QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.drawImage(QRectF(QPointF(0, 0), QSizeF(out.size())), planImage);
    painter.scale(factor, factor);

    const OverlayData overlay = buildOverlay(plan);
    QColor roomFill = t.signalBlue();
    roomFill.setAlpha(t.roomFillAlpha());
    painter.setPen(Qt::NoPen);
    painter.setBrush(roomFill);
    for (const OverlayRoom& room : overlay.rooms)
        painter.drawPath(room.floor);
    QColor wall = t.signalBlue();
    wall.setAlpha(t.overlayAlpha());
    painter.setBrush(wall);
    for (const OverlayWall& band : overlay.walls)
        painter.drawPath(band.band);
    painter.end();
    return out;
}

}
