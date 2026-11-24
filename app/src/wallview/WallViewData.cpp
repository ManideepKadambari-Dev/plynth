#include "WallViewData.h"

#include <plynth/core/Items.h>
#include <plynth/core/OpeningShapes.h>
#include <plynth/core/Openings.h>
#include <plynth/core/WallGeometry.h>
#include <plynth/core/catalog/ItemAssets.h>

#include <QObject>
#include <QStringList>

namespace plynth::app {

namespace {

// The words for a generator item whose add-on is off: "Needs" and the add-on names, or a plain fallback.
QString needsText(const plynth::GeneratorUse& use, const plynth::GeneratorLibrary* generators,
                  const std::optional<plynth::AddonCache>& addons)
{
    QStringList names;
    if (generators != nullptr)
        if (const plynth::GeneratorInfo* info = generators->find(use.name))
            for (const std::string& addon : plynth::generatorState(*info, addons).missingAddons)
                names << QString::fromStdString(plynth::addonDisplayName(addon));
    if (names.isEmpty())
        return QObject::tr("Add-on needed");
    return QObject::tr("Needs %1").arg(names.join(QStringLiteral(", ")));
}

}

// Builds the data for one wall of a floor, as seen from a side.
WallViewData buildWallViewData(const plynth::Floor& floor, const plynth::Wall& wall, plynth::WallSide side,
                               const QString& selectedOpening, const QString& selectedItem,
                               const plynth::AssetCatalog* catalog, const plynth::GeneratorLibrary* generators,
                               const std::optional<plynth::AddonCache>& addons)
{
    WallViewData data;
    const plynth::WallCurve curve = plynth::wallCurve(floor, wall);
    if (!curve.valid)
        return data;
    data.valid = true;
    data.wallId = QString::fromStdString(wall.id);
    data.length = curve.length;
    data.height = plynth::wallHeight(floor, wall);
    data.ceilingHeight = floor.ceilingHeight;
    data.curved = curve.curved;
    data.seenFromRight = side == plynth::WallSide::Right;
    data.selectedOpening = selectedOpening;
    const plynth::WallSpan span = plynth::wallSpan(floor, wall);
    data.hasSpan = true;
    data.spanStart = span.start;
    data.spanEnd = span.end;
    std::vector<plynth::Id> problems;
    for (const plynth::OpeningProblem& problem : plynth::openingProblems(floor))
        problems.push_back(problem.opening);

    for (const plynth::Opening& opening : floor.openings) {
        if (opening.wall != wall.id)
            continue;
        WallViewOpening entry;
        entry.id = QString::fromStdString(opening.id);
        entry.type = opening.type == plynth::OpeningType::Door ? 1 : opening.type == plynth::OpeningType::Window ? 2 : 0;
        entry.offset = opening.offset;
        entry.width = opening.width;
        entry.sill = opening.sill;
        entry.height = opening.height;
        for (const plynth::Vec2& corner : plynth::openingOutline(opening))
            entry.outline << QPointF(corner.x, corner.y);
        if (entry.outline.isEmpty()) {
            entry.outline << QPointF(opening.offset - opening.width / 2.0, opening.sill)
                          << QPointF(opening.offset + opening.width / 2.0, opening.sill)
                          << QPointF(opening.offset + opening.width / 2.0, opening.sill + opening.height)
                          << QPointF(opening.offset - opening.width / 2.0, opening.sill + opening.height);
        }
        for (const plynth::Id& id : problems)
            if (id == opening.id)
                entry.problem = true;
        data.openings.append(entry);
    }

    plynth::AssetLookup lookup;
    if (catalog != nullptr)
        lookup = plynth::assetLookupFor(*catalog);
    plynth::GeneratorItemLookup generatorLookup;
    if (generators != nullptr)
        generatorLookup = plynth::generatorItemLookupFor(*generators, addons);
    const std::vector<plynth::ItemProblem> itemIssues = plynth::itemProblems(floor, lookup, generatorLookup);
    for (const plynth::Item& item : floor.items) {
        if (!item.wall || *item.wall != wall.id)
            continue;
        const plynth::WallItemSpot spot = plynth::wallItemSpot(floor, item);
        if (!spot.valid || spot.side != side)
            continue;
        WallViewItem entry;
        entry.id = QString::fromStdString(item.id);
        entry.name = QString::fromStdString(item.asset);
        if (item.generator.has_value()) {
            entry.name = QString::fromStdString(item.generator->name);
            if (generators != nullptr)
                if (const plynth::GeneratorInfo* info = generators->find(item.generator->name))
                    entry.name = QString::fromStdString(info->name);
        } else if (catalog != nullptr) {
            if (const std::optional<plynth::AssetRecord> record = catalog->asset(item.asset))
                entry.name = QString::fromStdString(record->name);
        }
        entry.offset = spot.offset;
        entry.z = item.z;
        entry.width = item.size.width;
        entry.height = item.size.height;
        entry.centreHeight = plynth::itemCentreHeight(item);
        entry.selected = entry.id == selectedItem;
        for (const plynth::ItemProblem& issue : itemIssues) {
            if (issue.item != item.id)
                continue;
            entry.problem = true;
            if (issue.kind == plynth::ItemProblemKind::MissingAsset) {
                entry.missing = true;
                entry.missingText = QObject::tr("Missing asset");
            } else if (issue.kind == plynth::ItemProblemKind::MissingGenerator) {
                entry.missing = true;
                entry.missingText = QObject::tr("Missing generator");
            } else if (issue.kind == plynth::ItemProblemKind::GeneratorUnavailable && item.generator.has_value() &&
                       !entry.missing) {
                entry.missing = true;
                entry.missingText = needsText(*item.generator, generators, addons);
            }
        }
        entry.outline << QPointF(entry.offset - entry.width / 2.0, entry.z)
                      << QPointF(entry.offset + entry.width / 2.0, entry.z)
                      << QPointF(entry.offset + entry.width / 2.0, entry.z + entry.height)
                      << QPointF(entry.offset - entry.width / 2.0, entry.z + entry.height);
        data.items.append(entry);
    }
    return data;
}

}
