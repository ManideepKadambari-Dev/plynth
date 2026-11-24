#include "plynth/core/BuildResolve.h"

#include "plynth/core/OpeningAssets.h"

namespace plynth {

// Resolves a project with the library's choices.
ResolvedBuild resolveBuild(const Project& project, const BuildChoices& choices)
{
    ResolvedBuild out;
    out.project = project;
    VariationResult varied = variedPlan(project.plan, project.settings.materials, project.settings.variation, choices);
    out.project.plan = std::move(varied.plan);
    out.changes = std::move(varied.changes);

    const std::uint32_t seed = project.settings.variation.seed;
    for (Floor& floor : out.project.plan.floors) {
        for (Opening& opening : floor.openings) {
            if (opening.frame || !opening.autoFrame) continue;
            const std::optional<std::string> picked = chooseOpeningAsset(opening, true, choices.assets, seed);
            if (!picked) continue;
            opening.frame = picked;
            opening.autoFrame = false;
            out.frames.push_back({opening.id, *picked});
        }
        FloorDressing dressing;
        dressing.floor = floor.id;
        for (const Room& room : floor.rooms) {
            if (!room.dressing) continue;
            for (DressingPlacement& prop : dressRoom(floor, room, choices.assets, seed)) dressing.props.push_back(std::move(prop));
        }
        out.dressing.push_back(std::move(dressing));
    }
    return out;
}

}
