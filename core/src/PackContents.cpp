#include "plynth/core/Pack.h"

#include "plynth/core/BuildResolve.h"
#include "plynth/core/Generators.h"
#include "plynth/core/Materials.h"
#include "plynth/core/OpeningShapes.h"
#include "plynth/core/TrimProfiles.h"
#include "plynth/core/catalog/AssetCatalog.h"
#include "plynth/core/catalog/BuildChoices.h"
#include "plynth/core/catalog/MaterialCatalog.h"

#include <algorithm>
#include <set>
#include <utility>

namespace plynth {

namespace {

// Collects uses once each, in the order they are first seen.
struct Collector {
    PackContents contents;
    std::set<std::pair<int, std::string>> seen;

    void use(PackKind kind, const std::string& id)
    {
        if (!id.empty() && seen.insert({static_cast<int>(kind), id}).second) contents.used.push_back({kind, id});
    }

    // The items, generators and door and window frames of one plan.
    void plan(const Plan& plan)
    {
        for (const Floor& floor : plan.floors) {
            for (const Item& item : floor.items) {
                if (item.generator) use(PackKind::Generator, item.generator->name);
                else use(PackKind::Asset, item.asset);
            }
            for (const Opening& opening : floor.openings) {
                if (opening.frame) use(PackKind::Asset, *opening.frame);
            }
        }
    }

    // The opening shapes that are not built in.
    void shapes(const Plan& plan)
    {
        for (const Floor& floor : plan.floors) {
            for (const Opening& opening : floor.openings) {
                const OpeningShape* shape = findOpeningShape(opening.shape);
                if (shape == nullptr || !shape->builtIn()) use(PackKind::Shape, opening.shape);
            }
        }
    }
};

}

// Walks the plan as marked and as the build resolves it, and collects every library item either one names.
PackContents packContents(const Project& project, const LibraryPaths& library)
{
    Collector c;
    Status assetStatus;
    Status materialStatus;
    const auto assets = AssetCatalog::open(library, assetStatus);
    const auto materials = MaterialCatalog::open(library, materialStatus);
    // The same choices the build uses, so a seed's varied furniture, the picked doors and windows and the set
    // dressing props are in the pack as well as what is marked.
    const BuildChoices choices = buildChoicesFrom(assets.get(), materials.get());
    const ResolvedBuild resolved = resolveBuild(project, choices);

    c.plan(project.plan);
    c.plan(resolved.project.plan);
    for (const FloorDressing& floor : resolved.dressing) {
        for (const DressingPlacement& prop : floor.props) c.use(PackKind::Asset, prop.asset);
    }
    const GeneratorLibrary generators = loadGeneratorFolder(library.generators);
    for (const std::string& material : materialsUsed(project, generators)) c.use(PackKind::Material, material);
    for (const std::string& material : materialsUsed(resolved.project, generators)) c.use(PackKind::Material, material);
    c.shapes(project.plan);
    c.shapes(resolved.project.plan);
    const TrimSettings& trim = project.settings.trim;
    for (const TrimStyle* style : {&trim.skirting, &trim.cornice, &trim.casing, &trim.sill, &trim.railing}) {
        if (!style->enabled) continue;
        const TrimProfile* profile = findTrimProfile(style->profile);
        if (profile == nullptr || !profile->builtIn()) c.use(PackKind::TrimProfile, style->profile);
    }

    std::vector<RoomType> named;
    if (assets) named = assets->roomTypeList();
    const auto builtIn = builtInRoomTypes();
    std::set<std::string> rooms;
    for (const Floor& floor : project.plan.floors) {
        for (const Room& room : floor.rooms) {
            if (!room.type || room.type->empty() || !rooms.insert(*room.type).second) continue;
            if (std::any_of(builtIn.begin(), builtIn.end(), [&](const RoomType& t) { return t.id == *room.type; })) continue;
            RoomType type{*room.type, *room.type};
            for (const RoomType& known : named) {
                if (known.id == type.id) type.name = known.name;
            }
            c.contents.roomTypes.push_back(std::move(type));
        }
    }
    return c.contents;
}

}
