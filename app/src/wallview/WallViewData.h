#pragma once

#include <plynth/core/Plan.h>
#include <plynth/core/catalog/AssetCatalog.h>

#include <QPolygonF>
#include <QString>
#include <QVector>

namespace plynth::app {

// One opening on the wall face. Outline points are (u, v) in metres: u along the wall's centre line from its
// from point, v up from the floor.
struct WallViewOpening {
    QString id;
    int type = 0; // 0 plain, 1 door, 2 window
    double offset = 0.0;
    double width = 0.0;
    double sill = 0.0;
    double height = 0.0;
    QPolygonF outline;
    bool problem = false;
};

// An item mounted on the wall face: its front box, from the offset minus half its width to the offset plus
// half, and from its bottom z up by its height. problem marks a warning from itemProblems, missing a gone asset.
struct WallViewItem {
    QString id;
    QString name;
    QPolygonF outline;
    double offset = 0.0;
    double z = 0.0;
    double width = 0.0;
    double height = 0.0;
    double centreHeight = 0.0;
    bool selected = false;
    bool problem = false;
    bool missing = false;
};

// Everything the wall view draws, in metres on the wall face. The view reads this and nothing else.
struct WallViewData {
    bool valid = false;
    QString wallId;
    double length = 0.0;         // along the centre line
    double height = 0.0;         // the wall's own height, or the ceiling height
    double ceilingHeight = 0.0;  // the floor's ceiling height
    bool curved = false;
    bool seenFromRight = true;   // the face seen from the right side of the wall; false mirrors u
    bool hasSpan = false;
    double spanStart = 0.0;      // the free part of the wall: openings go between these two u values
    double spanEnd = 0.0;
    QString selectedOpening;
    QVector<WallViewOpening> openings;
    QVector<WallViewItem> items;
};

// Builds the data for one wall of a floor, as seen from a side. selectedOpening and selectedItem are the ids to
// show as selected, or empty. With a catalog the items are named and checked against it.
WallViewData buildWallViewData(const plynth::Floor& floor, const plynth::Wall& wall, plynth::WallSide side,
                               const QString& selectedOpening, const QString& selectedItem = QString(),
                               const plynth::AssetCatalog* catalog = nullptr);

}
