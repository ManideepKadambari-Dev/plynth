#pragma once

#include "SquareFixtures.h"

#include <plynth/core/BuildJob.h>
#include <plynth/core/MaterialDefaults.h>
#include <plynth/core/Project.h>

#include <optional>
#include <string>
#include <string_view>

namespace plynth::test {

// An item of the build fixtures: at (x, y), standing in room r1.
inline Item buildItem(const std::string& id, const std::string& asset, double x, double y, const ItemSize& size)
{
    Item i;
    i.id = id;
    i.asset = asset;
    i.room = "r1";
    i.x = x;
    i.y = y;
    i.size = size;
    return i;
}

// Clears every project default material, so the surfaces of the project have no material unless a wall or
// room names one.
inline void clearMaterialDefaults(Project& project)
{
    for (const Surface surface : allSurfaces()) setMaterialDefault(project.settings.materials, surface, std::nullopt);
}

// A project with a square room r1 (walls w1 to w4), a fitting door o1, a window o2 that reaches past its
// wall, a room r2 whose walls do not close, and the items sofa i1, chair i2, lamp i3 and a second sofa i4 that
// overlaps the first. The project defaults have no materials.
inline Project buildProject()
{
    Project project = makeNewProject();
    Floor f = squareFloorWithWalls();
    Room r1;
    r1.id = "r1";
    r1.walls = {"w1", "w2", "w3", "w4"};
    Room r2;
    r2.id = "r2";
    r2.walls = {"w1", "w2"};
    f.rooms = {r1, r2};

    Opening door = doorOn("w1", 2.0);
    door.id = "o1";
    Opening window;
    window.id = "o2";
    window.wall = "w2";
    window.offset = 0.2;
    window.type = OpeningType::Window;
    window.width = 1.2;
    window.height = 1.2;
    window.sill = 0.9;
    f.openings = {door, window};

    f.items = {buildItem("i1", "sofa", 2.0, 2.5, {1.8, 0.9, 0.8}), buildItem("i2", "chair", 0.8, 1.0, {0.5, 0.5, 0.9}),
               buildItem("i3", "lamp", 3.4, 0.8, {0.4, 0.4, 1.6}), buildItem("i4", "sofa", 2.3, 2.6, {1.8, 0.9, 0.8})};
    project.plan.floors[0] = f;
    clearMaterialDefaults(project);
    return project;
}

// A materials source that has the sample materials except red_brick, plus "tile", and knows "plaster" but says
// it is broken.
inline BuildMaterialSource sampleMaterialSource()
{
    return [](std::string_view id) -> std::optional<BuildMaterialState> {
        if (id == "plaster") {
            BuildMaterialState broken;
            broken.material.id = "plaster";
            broken.broken = true;
            broken.problem = "The base color map is missing.";
            return broken;
        }
        const std::string name(id);
        if (name != "white_paint" && name != "oak_planks" && name != "painted_wood" && name != "clear_glass" &&
            name != "brushed_steel" && name != "tile") {
            return std::nullopt;
        }
        BuildMaterialState state;
        const std::string dir = "D:/Library/materials/items/" + name + "/textures/";
        state.material.id = name;
        state.material.tileSize = {2.0, 1.0};
        state.material.baseColor = dir + "base_color.png";
        state.material.normal = dir + "normal.png";
        state.material.roughness = dir + "roughness.png";
        state.material.metallic = dir + "metallic.png";
        if (name == "clear_glass") state.material.opacity = dir + "opacity.png";
        return state;
    };
}

// A source that has the sofa, knows the lamp but says it is broken, and has nothing else.
inline BuildAssetSource sampleAssetSource()
{
    return [](std::string_view id) -> std::optional<BuildAssetState> {
        if (id == "sofa") return BuildAssetState{"D:/Library/assets/items/sofa/asset.blend", false, ""};
        if (id == "lamp") return BuildAssetState{"D:/Library/assets/items/lamp/asset.blend", true, "asset.blend is missing."};
        return std::nullopt;
    };
}

}
