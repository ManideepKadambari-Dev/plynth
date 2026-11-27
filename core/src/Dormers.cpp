#include "plynth/core/Dormers.h"

#include "parts/DormerShape.h"

#include <optional>

namespace plynth {

// Works out the floor's roofs and where the dormer stands on them.
DormerPlacement dormerPlacement(const Plan& plan, const Floor& floor, const Dormer& dormer,
                                const std::vector<double>& levels)
{
    std::vector<RoofBuild> roofs;
    if (const std::optional<std::size_t> index = floorIndex(plan, floor.id)) roofs = floorRoofs(plan, *index, levels).roofs;
    const dormers::Shape shape = dormers::shapeOf(dormer, floor.id, roofs, roofAreasOf(plan, floor.id));

    DormerPlacement out;
    out.status = shape.status;
    out.roofIndex = shape.roofIndex;
    out.front = shape.front;
    out.facing = {-shape.back.x, -shape.back.y};
    out.footZ = shape.footZ;
    if (shape.slope > 0.0) out.footprint = shape.atRing(shape.footprint);
    return out;
}

// Checks the dormer by itself on an empty plan, so the rules and sentences are checkPlan's own.
Status checkDormer(const Dormer& dormer)
{
    Plan plan = makeEmptyPlan();
    Dormer copy = dormer;
    copy.id = std::string(kDormerIdPrefix) + "1";
    plan.floors.front().dormers.push_back(copy);
    Status status = checkPlan(plan);
    status.where.clear();
    return status;
}

}
