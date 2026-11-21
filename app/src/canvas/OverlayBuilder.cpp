#include "OverlayBuilder.h"

#include <plynth/core/Items.h>
#include <plynth/core/RoomTypes.h>
#include <plynth/core/Rooms.h>
#include <plynth/core/WallGeometry.h>
#include <plynth/core/catalog/ItemAssets.h>

#include <QObject>
#include <QPolygonF>
#include <QStringList>

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
OverlayData buildOverlay(const plynth::Plan& plan, const plynth::AssetCatalog* catalog)
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

    addOverlayOpenings(overlay, floor, ppm);

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

    plynth::AssetLookup lookup;
    if (catalog != nullptr)
        lookup = plynth::assetLookupFor(*catalog);
    const std::vector<plynth::ItemProblem> problems = plynth::itemProblems(floor, lookup);
    for (const plynth::Item& item : floor.items) {
        OverlayItem entry;
        entry.id = fromCore(item.id);
        const plynth::ItemFootprint footprint = plynth::itemFootprint(item);
        for (const plynth::Vec2& corner : footprint.corners)
            entry.footprint << QPointF(corner.x * ppm, corner.y * ppm);
        entry.front = QPointF(footprint.front.x, footprint.front.y);
        entry.wall = item.wall.has_value();
        entry.label = fromCore(item.asset);
        if (catalog != nullptr)
            if (const std::optional<plynth::AssetRecord> record = catalog->asset(item.asset))
                entry.label = fromCore(record->name);
        QStringList sentences;
        for (const plynth::ItemProblem& problem : problems) {
            if (problem.item != item.id)
                continue;
            entry.problem = true;
            sentences << fromCore(problem.message);
            if (problem.kind == plynth::ItemProblemKind::MissingAsset) {
                entry.missing = true;
                entry.label = QObject::tr("Missing asset");
                entry.detail = fromCore(item.asset);
            }
        }
        entry.tip = sentences.join(QLatin1Char('\n'));
        overlay.items.append(entry);
    }
    return overlay;
}

}
