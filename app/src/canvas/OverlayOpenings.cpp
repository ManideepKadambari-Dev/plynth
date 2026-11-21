#include "OverlayBuilder.h"

#include <plynth/core/Openings.h>

#include <QPolygonF>

#include <cmath>
#include <numbers>

namespace plynth::app {

namespace {

constexpr int kArcSteps = 16;

// A position in metres as a point in image pixels.
QPointF px(plynth::Vec2 v, double ppm)
{
    return QPointF(v.x * ppm, v.y * ppm);
}

// The unit direction from a to b, or zero when they are the same place.
plynth::Vec2 unit(plynth::Vec2 a, plynth::Vec2 b)
{
    const double length = plynth::distance(a, b);
    if (length < 1e-12)
        return {};
    return {(b.x - a.x) / length, (b.y - a.y) / length};
}

// A line across the wall band at a jamb: half the thickness each side of the centre line.
QLineF jamb(plynth::Vec2 at, plynth::Vec2 tangent, double half, double ppm)
{
    const plynth::Vec2 n = plynth::rightOf(tangent);
    return QLineF(px({at.x + n.x * half, at.y + n.y * half}, ppm), px({at.x - n.x * half, at.y - n.y * half}, ppm));
}

// The wall centre line from jamb to jamb, moved sideways by a distance (positive to the right of the wall).
QPolygonF shifted(const std::vector<plynth::Vec2>& line, double distance, double ppm)
{
    QPolygonF out;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const plynth::Vec2 before = line[i == 0 ? 0 : i - 1];
        const plynth::Vec2 after = line[i + 1 < line.size() ? i + 1 : i];
        const plynth::Vec2 n = plynth::rightOf(unit(before, after));
        out << px({line[i].x + n.x * distance, line[i].y + n.y * distance}, ppm);
    }
    return out;
}

}

// Works out where an opening is drawn on the plan, from core's placement. Invalid when it has no place.
OverlayOpening makeOverlayOpening(const plynth::Floor& floor, const plynth::Opening& opening, double ppm)
{
    OverlayOpening out;
    const plynth::OpeningPlacement place = plynth::openingPlacement(floor, opening);
    if (!place.valid)
        return out;
    out.valid = true;
    out.id = QString::fromStdString(opening.id);
    out.type = opening.type == plynth::OpeningType::Door ? 1 : opening.type == plynth::OpeningType::Window ? 2 : 0;
    for (const plynth::Vec2& corner : place.footprint)
        out.footprint << px(corner, ppm);
    const double half = place.thickness / 2.0;
    out.startJamb = jamb(place.start, place.startTangent, half, ppm);
    out.endJamb = jamb(place.end, place.endTangent, half, ppm);

    if (out.type == 1) {
        const bool hingeAtStart = opening.hinge == plynth::HingeEnd::Start;
        const plynth::Vec2 hinge = hingeAtStart ? place.start : place.end;
        const plynth::Vec2 other = hingeAtStart ? place.end : place.start;
        const plynth::Vec2 along = unit(hinge, other);
        plynth::Vec2 side = plynth::rightOf(unit(place.start, place.end));
        if (opening.swing == plynth::WallSide::Left)
            side = {-side.x, -side.y};
        const double radius = opening.width;
        const plynth::Vec2 leafEnd{hinge.x + side.x * radius, hinge.y + side.y * radius};
        out.leaf = QLineF(px(hinge, ppm), px(leafEnd, ppm));
        for (int i = 0; i <= kArcSteps; ++i) {
            const double angle = (std::numbers::pi / 2.0) * i / kArcSteps;
            const double c = std::cos(angle) * radius;
            const double s = std::sin(angle) * radius;
            out.arc << px({hinge.x + along.x * c + side.x * s, hinge.y + along.y * c + side.y * s}, ppm);
        }
    } else if (out.type == 2) {
        out.glassA = shifted(place.centreLine, place.thickness / 6.0, ppm);
        out.glassB = shifted(place.centreLine, -place.thickness / 6.0, ppm);
    }
    return out;
}

// Adds every opening of the floor to the overlay, marking the ones that no longer fit their wall.
void addOverlayOpenings(OverlayData& overlay, const plynth::Floor& floor, double ppm)
{
    std::vector<plynth::Id> problems;
    for (const plynth::OpeningProblem& problem : plynth::openingProblems(floor))
        problems.push_back(problem.opening);
    for (const plynth::Opening& opening : floor.openings) {
        OverlayOpening entry = makeOverlayOpening(floor, opening, ppm);
        if (!entry.valid)
            continue;
        for (const plynth::Id& id : problems)
            if (id == opening.id)
                entry.problem = true;
        overlay.openings.append(entry);
    }
}

}
