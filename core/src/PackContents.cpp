#include "plynth/core/Pack.h"

#include "plynth/core/Generators.h"
#include "plynth/core/Materials.h"
#include "plynth/core/OpeningShapes.h"
#include "plynth/core/catalog/AssetCatalog.h"

#include <algorithm>
#include <set>
#include <utility>

namespace plynth {

// Walks the plan and collects every library item it names.
PackContents packContents(const Project& project, const LibraryPaths& library)
{
    PackContents contents;
    std::set<std::pair<int, std::string>> seen;
    const auto use = [&](PackKind kind, const std::string& id) {
        if (!id.empty() && seen.insert({static_cast<int>(kind), id}).second) contents.used.push_back({kind, id});
    };
    for (const Floor& floor : project.plan.floors) {
        for (const Item& item : floor.items) {
            if (item.generator) use(PackKind::Generator, item.generator->name);
            else use(PackKind::Asset, item.asset);
        }
    }
    const GeneratorLibrary generators = loadGeneratorFolder(library.generators);
    for (const std::string& material : materialsUsed(project, generators)) use(PackKind::Material, material);
    for (const Floor& floor : project.plan.floors) {
        for (const Opening& opening : floor.openings) {
            const OpeningShape* shape = findOpeningShape(opening.shape);
            if (shape == nullptr || !shape->builtIn()) use(PackKind::Shape, opening.shape);
        }
    }

    std::vector<RoomType> named;
    Status status;
    if (auto catalog = AssetCatalog::open(library, status)) named = catalog->roomTypeList();
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
            contents.roomTypes.push_back(std::move(type));
        }
    }
    return contents;
}

}
