#include "OverlayBuilder.h"

#include <plynth/core/Canopies.h>
#include <plynth/core/Geometry.h>
#include <plynth/core/Items.h>
#include <plynth/core/Roofs.h>
#include <plynth/core/Storeys.h>
#include <plynth/core/RoomTypes.h>
#include <plynth/core/Rooms.h>
#include <plynth/core/WallGeometry.h>
#include <plynth/core/catalog/ItemAssets.h>

#include <QObject>
#include <QPolygonF>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <numbers>

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

// Turns a roof section into the area, label and direction arrow the canvas draws, in image pixels.
OverlayRoof makeOverlayRoof(const plynth::RoofArea& roof, double ppm)
{
    OverlayRoof out;
    out.id = QStringLiteral("%1:%2").arg(fromCore(roof.floor)).arg(roof.index);
    out.area.setFillRule(Qt::OddEvenFill);
    out.area.addPolygon(toPolygon(roof.area.outline, ppm));
    out.area.closeSubpath();
    for (const std::vector<plynth::Vec2>& hole : roof.area.holes) {
        out.area.addPolygon(toPolygon(hole, ppm));
        out.area.closeSubpath();
    }
    out.labelPoint = QPointF(roof.labelPoint.x * ppm, roof.labelPoint.y * ppm);

    const plynth::RoofSettings& s = roof.settings;
    const plynth::RoofType* type = plynth::findRoofType(s.type);
    QString name = type != nullptr ? fromCore(std::string(type->name())) : fromCore(s.type);
    const QString pitch = QStringLiteral("%1°").arg(s.pitch, 0, 'f', s.pitch == std::floor(s.pitch) ? 0 : 1);
    if (type == nullptr)
        out.label = QObject::tr("%1 (unknown type)").arg(name);
    else if (type->usesParapet() && s.parapet > 0.0)
        out.label = QObject::tr("%1, parapet %2 m").arg(name).arg(s.parapet, 0, 'f', 2);
    else if (type->usesPitch())
        out.label = QStringLiteral("%1 %2").arg(name, pitch);
    else
        out.label = name;
    out.tip = QObject::tr("Roof section %1: %2. Click it to select it.").arg(out.id, out.label);
    // A sliver gets the hatch but no label, which would only cover the plan.
    if (plynth::planAreaSize(roof.area) < 0.25)
        out.label.clear();

    if (type == nullptr || !type->usesDirection())
        return out;
    double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
    for (const plynth::Vec2& p : roof.area.outline) {
        x0 = std::min(x0, p.x);
        x1 = std::max(x1, p.x);
        y0 = std::min(y0, p.y);
        y1 = std::max(y1, p.y);
    }
    plynth::Vec2 ridge{1.0, 0.0};
    if (s.direction) {
        const double r = *s.direction * std::numbers::pi / 180.0;
        ridge = {std::cos(r), std::sin(r)};
    } else if ((x1 - x0) < (y1 - y0)) {
        ridge = {0.0, 1.0};
    }
    const bool shed = s.type == plynth::kShedRoofId;
    plynth::Vec2 along = ridge;
    if (shed && !s.direction)
        along = std::fabs(ridge.x) >= std::fabs(ridge.y) ? plynth::Vec2{0.0, 1.0} : plynth::Vec2{1.0, 0.0};
    const double extent = std::fabs(along.x) * (x1 - x0) + std::fabs(along.y) * (y1 - y0);
    const double half = extent * 0.35 * ppm;
    const QPointF direction(along.x, along.y);
    out.arrow = true;
    out.ridge = !shed;
    out.arrowFrom = out.labelPoint - direction * half;
    out.arrowTo = out.labelPoint + direction * half;
    return out;
}

// Turns every canopy of the floor into its outline, label and tip in image pixels, with the posts that hold it up
// (canopyPosts) shown as its posts.
void addOverlayCanopies(OverlayData& overlay, const plynth::Floor& floor, double ppm)
{
    for (const plynth::Canopy& canopy : floor.canopies) {
        if (canopy.outline.size() < 3)
            continue;
        OverlayCanopy entry;
        entry.id = fromCore(canopy.id);
        std::vector<plynth::Vec2> ring;
        ring.reserve(canopy.outline.size());
        for (const plynth::PlanXY& corner : canopy.outline)
            ring.push_back(plynth::Vec2{corner.x, corner.y});
        const QPolygonF polygon = toPolygon(ring, ppm);
        entry.area.setFillRule(Qt::OddEvenFill);
        entry.area.addPolygon(polygon);
        entry.area.closeSubpath();
        entry.labelPoint = polygon.boundingRect().center();
        entry.label = entry.id;
        const plynth::RoofType* type = plynth::findRoofType(canopy.roof.type);
        const QString roof = type != nullptr ? fromCore(std::string(type->name()))
                                             : QObject::tr("%1 (unknown type)").arg(fromCore(canopy.roof.type));
        entry.tip = QObject::tr("Canopy %1: %2 roof, underside %3 m above the floor. Click it to select it.")
                        .arg(entry.id, roof)
                        .arg(canopy.height, 0, 'f', 2);
        for (const plynth::Id& postId : plynth::canopyPosts(floor, canopy)) {
            const plynth::Item* found = plynth::findItem(floor, postId);
            if (found == nullptr)
                continue;
            const plynth::Item& item = *found;
            QPolygonF post;
            for (const plynth::Vec2& corner : plynth::itemFootprint(item).corners)
                post << QPointF(corner.x * ppm, corner.y * ppm);
            entry.posts.append(post);
        }
        overlay.canopies.append(entry);
    }
}

// Gives the canvas the points, walls and rooms of the first floor.
OverlayData buildOverlay(const plynth::Plan& plan, const plynth::AssetCatalog* catalog,
                         const plynth::GeneratorLibrary* generators, const std::optional<plynth::AddonCache>* addons)
{
    if (plan.floors.empty())
        return OverlayData();
    return buildOverlay(plan, plan.floors.front().id, catalog, generators, addons);
}

// Gives the canvas the points, walls and rooms of one floor, and the faint walls of the floor below.
OverlayData buildOverlay(const plynth::Plan& plan, const std::string& floorId, const plynth::AssetCatalog* catalog,
                         const plynth::GeneratorLibrary* generators, const std::optional<plynth::AddonCache>* addons)
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
            for (const std::vector<plynth::Vec2>& ring : plynth::wallOutline(*below, wall)) {
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

    for (const plynth::RoofArea& roof : plynth::roofAreasOf(plan, floorId))
        overlay.roofs.append(makeOverlayRoof(roof, ppm));

    addOverlayCanopies(overlay, floor, ppm);

    if (const std::optional<std::size_t> index = plynth::floorIndex(plan, floorId)) {
        overlay.overhang.setFillRule(Qt::OddEvenFill);
        for (const plynth::PlanArea& area : plynth::overhangArea(plan, *index)) {
            overlay.overhang.addPolygon(toPolygon(area.outline, ppm));
            overlay.overhang.closeSubpath();
            for (const std::vector<plynth::Vec2>& hole : area.holes) {
                overlay.overhang.addPolygon(toPolygon(hole, ppm));
                overlay.overhang.closeSubpath();
            }
        }
        if (const plynth::Floor* above = plynth::floorAbove(plan, floorId))
            overlay.overhangTip = QObject::tr("%1 reaches past this floor here. The build closes its underside with a soffit.")
                                      .arg(fromCore(above->name.empty() ? above->id : above->name));
    }

    for (const plynth::LightMarker& light : floor.lights) {
        OverlayLight entry;
        entry.id = fromCore(light.id);
        entry.position = QPointF(light.x * ppm, light.y * ppm);
        entry.kind = light.kind == plynth::LightKind::Wall ? 1 : light.kind == plynth::LightKind::Standing ? 2 : 0;
        const QString kind = entry.kind == 1 ? QObject::tr("Wall light")
                             : entry.kind == 2 ? QObject::tr("Standing lamp")
                                               : QObject::tr("Ceiling light");
        entry.tip = QObject::tr("%1 %2, %3 m above the floor").arg(kind, entry.id).arg(light.z, 0, 'f', 2);
        overlay.lights.append(entry);
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
        entry.outline.setFillRule(Qt::WindingFill);
        for (const std::vector<plynth::Vec2>& ring : plynth::wallOutline(floor, wall)) {
            entry.outline.addPolygon(toPolygon(ring, ppm));
            entry.outline.closeSubpath();
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
    plynth::GeneratorItemLookup generatorLookup;
    if (generators != nullptr) {
        static const std::optional<plynth::AddonCache> noCache;
        generatorLookup = plynth::generatorItemLookupFor(*generators, addons != nullptr ? *addons : noCache);
    }
    const std::vector<plynth::ItemProblem> problems = plynth::itemProblems(floor, lookup, generatorLookup);
    for (const plynth::Item& item : floor.items) {
        OverlayItem entry;
        entry.id = fromCore(item.id);
        const plynth::ItemFootprint footprint = plynth::itemFootprint(item);
        for (const plynth::Vec2& corner : footprint.corners)
            entry.footprint << QPointF(corner.x * ppm, corner.y * ppm);
        entry.front = QPointF(footprint.front.x, footprint.front.y);
        entry.wall = item.wall.has_value();
        entry.label = fromCore(item.asset);
        if (item.generator.has_value()) {
            entry.label = fromCore(item.generator->name);
            if (generators != nullptr)
                if (const plynth::GeneratorInfo* info = generators->find(item.generator->name))
                    entry.label = fromCore(info->name);
        } else if (catalog != nullptr) {
            if (const std::optional<plynth::AssetRecord> record = catalog->asset(item.asset))
                entry.label = fromCore(record->name);
        }
        QStringList sentences;
        for (const plynth::ItemProblem& problem : problems) {
            if (problem.item != item.id)
                continue;
            entry.problem = true;
            sentences << fromCore(problem.message);
            if (problem.kind == plynth::ItemProblemKind::GeneratorUnavailable)
                entry.unavailable = true;
            else
                entry.otherProblem = true;
            if (problem.kind == plynth::ItemProblemKind::MissingAsset) {
                entry.missing = true;
                entry.label = QObject::tr("Missing asset");
                entry.detail = fromCore(item.asset);
            }
            if (problem.kind == plynth::ItemProblemKind::MissingGenerator && item.generator.has_value()) {
                entry.missing = true;
                entry.label = QObject::tr("Missing generator");
                entry.detail = fromCore(item.generator->name);
            }
            if (problem.kind == plynth::ItemProblemKind::GeneratorUnavailable && item.generator.has_value() &&
                generators != nullptr) {
                if (const plynth::GeneratorInfo* info = generators->find(item.generator->name)) {
                    const plynth::GeneratorState state =
                        plynth::generatorState(*info, addons != nullptr ? *addons : std::optional<plynth::AddonCache>{});
                    QStringList names;
                    for (const std::string& addon : state.missingAddons)
                        names << fromCore(plynth::addonDisplayName(addon));
                    if (!names.isEmpty()) {
                        entry.missing = true;
                        entry.detail = entry.label;
                        entry.label = QObject::tr("Needs %1").arg(names.join(QStringLiteral(", ")));
                    }
                }
            }
        }
        entry.tip = sentences.join(QLatin1Char('\n'));
        overlay.items.append(entry);
    }
    return overlay;
}

}
