#include "OverlayBuilder.h"

#include <plynth/core/RoomTypes.h>
#include <plynth/core/Rooms.h>
#include <plynth/core/WallGeometry.h>

#include <QPolygonF>

namespace plynth::app {

namespace {

// Turns a core sentence into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// A closed outline in metres as a polygon in image pixels.
QPolygonF toPolygon(const std::vector<plynth::Vec2>& ring, double ppm)
{
    QPolygonF polygon;
    polygon.reserve(static_cast<qsizetype>(ring.size()));
    for (const plynth::Vec2& corner : ring)
        polygon << QPointF(corner.x * ppm, corner.y * ppm);
    return polygon;
}

}

// Gives the canvas the points, walls and rooms of the first floor, in image pixels.
OverlayData buildOverlay(const plynth::Plan& plan)
{
    OverlayData overlay;
    if (!plan.scale.pixelsPerMetre.has_value() || plan.floors.empty())
        return overlay;
    const double ppm = *plan.scale.pixelsPerMetre;
    const plynth::Floor& floor = plan.floors.front();

    for (const plynth::Point& point : floor.points)
        overlay.points.append({fromCore(point.id), QPointF(point.x * ppm, point.y * ppm)});

    for (const plynth::Wall& wall : floor.walls) {
        OverlayWall entry;
        entry.id = fromCore(wall.id);
        entry.band.setFillRule(Qt::WindingFill);
        for (const std::vector<plynth::Vec2>& ring : plynth::wallBand(floor, wall)) {
            entry.band.addPolygon(toPolygon(ring, ppm));
            entry.band.closeSubpath();
        }
        if (!entry.band.isEmpty())
            overlay.walls.append(entry);
    }

    for (const plynth::Room& room : floor.rooms) {
        const plynth::RoomShape shape = plynth::roomShape(floor, room);
        if (!shape.closed || shape.floorArea.empty())
            continue;
        OverlayRoom entry;
        entry.id = fromCore(room.id);
        entry.floor.setFillRule(Qt::OddEvenFill);
        for (const std::vector<plynth::Vec2>& ring : shape.floorArea) {
            entry.floor.addPolygon(toPolygon(ring, ppm));
            entry.floor.closeSubpath();
        }
        entry.label = QPointF(shape.labelPoint.x * ppm, shape.labelPoint.y * ppm);
        entry.name = fromCore(plynth::roomTypeName(room.type));
        entry.area = QStringLiteral("%1 m²").arg(shape.area, 0, 'f', 2);
        overlay.rooms.append(entry);
    }
    return overlay;
}

}
