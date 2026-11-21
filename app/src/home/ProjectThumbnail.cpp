#include "ProjectThumbnail.h"

#include "canvas/OverlayBuilder.h"
#include "theme/Theme.h"

#include <QImageReader>
#include <QPainter>

#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Settings.h>

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

// Reads the project and its plan image, draws the picture and saves it as a PNG under the settings folder.
bool writeRecentThumbnail(const std::filesystem::path& projectFolder)
{
    const plynth::ProjectLoad load = plynth::loadProject(projectFolder);
    if (!load.status.ok() || load.project.plan.floors.empty() || !load.project.plan.floors.front().image.has_value())
        return false;
    const std::filesystem::path imageFile = plynth::resolveImagePath(projectFolder, *load.project.plan.floors.front().image);
    QImageReader reader(QString::fromStdWString(imageFile.wstring()));
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    const QImage thumb = renderProjectThumbnail(load.project.plan, image, 320);
    if (thumb.isNull())
        return false;
    const std::filesystem::path file = plynth::recentThumbnailPath(projectFolder);
    std::error_code ignored;
    std::filesystem::create_directories(file.parent_path(), ignored);
    return thumb.save(QString::fromStdWString(file.wstring()), "PNG");
}

}
