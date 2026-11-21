#include "WallViewData.h"

#include <plynth/core/OpeningShapes.h>
#include <plynth/core/Openings.h>
#include <plynth/core/WallGeometry.h>

namespace plynth::app {

// Builds the data for one wall of a floor, as seen from a side.
WallViewData buildWallViewData(const plynth::Floor& floor, const plynth::Wall& wall, plynth::WallSide side,
                               const QString& selectedOpening)
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
    return data;
}

}
