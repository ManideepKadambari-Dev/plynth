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

// Turns a stair layout into polygons in image pixels.
OverlayStair makeOverlayStair(const plynth::StairLayout& layout, double ppm)
{
    OverlayStair stair;
    stair.id = fromCore(layout.stair);
    for (const plynth::StairStep& step : layout.steps) {
        OverlayStairStep entry;
        entry.outline = toPolygon(step.outline, ppm);
        entry.part = step.part == plynth::StairPart::Step ? 0 : step.part == plynth::StairPart::Landing ? 1 : 2;
        stair.steps.append(entry);
    }
    stair.footprint = toPolygon(layout.footprint, ppm);
    stair.walkLine = toPolygon(layout.walkLine, ppm);
    stair.arrival = QPointF(layout.arrival.x * ppm, layout.arrival.y * ppm);
    if (!stair.walkLine.isEmpty())
        stair.origin = stair.walkLine.first();
    return stair;
}

// Gives the canvas the points, walls and rooms of the first floor.
OverlayData buildOverlay(const plynth::Plan& plan, const plynth::AssetCatalog* catalog)
{
    if (plan.floors.empty())
        return OverlayData();
    return buildOverlay(plan, plan.floors.front().id, catalog);
}

// Gives the canvas the points, walls and rooms of one floor, and the faint walls of the floor below.
OverlayData buildOverlay(const plynth::Plan& plan, const std::string& floorId, const plynth::AssetCatalog* catalog)
{
    OverlayData overlay;
    const plynth::Floor* shown = plynth::findFloor(plan, floorId);
    if (!plan.scale.pixelsPerMetre.has_value() || shown == nullptr)
        return overlay;
    const double ppm = *plan.scale.pixelsPerMetre;
    const plynth::Floor& floor = *shown;
    overlay.imageOffset = QPointF(floor.imageOffset.x * ppm, floor.imageOffset.y * ppm);

    if (const plynth::Floor* below = plynth::floorBelow(plan, floorId)) {
        overlay.below.setFillRule(Qt::WindingFill);
        for (const plynth::Wall& wall : below->walls)
            for (const std::vector<plynth::Vec2>& ring : plynth::wallBand(*below, wall)) {
                overlay.below.addPolygon(toPolygon(ring, ppm));
                overlay.below.closeSubpath();
            }
    }

    const std::vector<plynth::StairProblem> stairProblems = plynth::stairProblems(plan, floor);
    for (const plynth::Stair& stair : floor.stairs) {
        const plynth::StairLayout layout = plynth::stairLayout(plan, floor, stair);
        if (!layout.status.ok())
            continue;
        OverlayStair entry = makeOverlayStair(layout, ppm);
        QStringList sentences;
        for (const plynth::StairProblem& problem : stairProblems)
            if (problem.stair == stair.id) {
                entry.problem = true;
                sentences << fromCore(problem.message);
            }
        entry.tip = sentences.join(QLatin1Char('\n'));
        overlay.stairs.append(entry);
    }
    for (const plynth::StairLayout& layout : plynth::stairsArrivingAt(plan, floorId)) {
        OverlayStair entry = makeOverlayStair(layout, ppm);
        const plynth::Floor* from = plynth::findFloor(plan, layout.fromFloor);
        entry.from = fromCore(from != nullptr ? from->name : layout.fromFloor);
        overlay.arriving.append(entry);
    }

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
