#pragma once

#include <plynth/core/ProjectJson.h>

#include <nlohmann/json.hpp>

#include <initializer_list>

namespace plynth::test {

// Removes the fields plan format 8 added from a plan file's JSON and marks it version 7.
inline void planBackTo7(Json& plan)
{
    plan["version"] = 7;
    for (Json& floor : plan["floors"]) {
        for (const char* key : {"decks", "railings", "canopies", "dormers", "light_strips"}) floor.erase(key);
        for (Json& wall : floor["walls"]) wall.erase("cladding");
        for (Json& opening : floor["openings"]) opening.erase("fill");
        for (Json& item : floor["items"]) item.erase("on");
        for (Json& stair : floor["stairs"]) {
            for (const char* key : {"rise", "cheeks", "railing"}) stair.erase(key);
        }
    }
    const auto dropRoofFields = [](Json& settings) {
        for (const char* key : {"second_pitch", "break_ratio", "ridge_offset", "rise"}) settings.erase(key);
    };
    if (plan.contains("roof")) dropRoofFields(plan["roof"]);
    if (plan.contains("roof_sections")) {
        for (Json& section : plan["roof_sections"]) dropRoofFields(section["settings"]);
    }
    for (const char* key : {"ground_floor", "site", "foundation"}) plan.erase(key);
}

// Removes the fields project format 7 added from a project file's JSON and marks it version 6.
inline void projectBackTo6(Json& project)
{
    project["version"] = 6;
    if (!project.contains("materials")) return;
    for (const char* key : {"deck", "foundation", "ground", "paving", "fence", "boundary_wall"}) project["materials"].erase(key);
}

}
