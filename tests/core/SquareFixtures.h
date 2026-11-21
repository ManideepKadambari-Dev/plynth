#pragma once

#include "TestSupport.h"

#include <plynth/core/Plan.h>
#include <plynth/core/Project.h>

#include <string>

namespace plynth::test {

// A floor holding the square (0,0), (4,0), (4,4), (0,4) with 0.2 m walls w1 to w4 drawn in that order.
inline Floor squareFloorWithWalls()
{
    Floor f;
    f.id = "f1";
    f.name = "Floor 1";
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 4.0, 0.0, 0.0}, {"p3", 4.0, 4.0, 0.0}, {"p4", 0.0, 4.0, 0.0}};
    const char* ends[4][2] = {{"p1", "p2"}, {"p2", "p3"}, {"p3", "p4"}, {"p4", "p1"}};
    for (int i = 0; i < 4; ++i) {
        Wall w;
        w.id = "w" + std::to_string(i + 1);
        w.from = ends[i][0];
        w.to = ends[i][1];
        w.thickness = 0.2;
        f.walls.push_back(w);
    }
    return f;
}

// The square as a project.
inline Project squareProjectWithWalls()
{
    Project p = makeNewProject();
    p.plan.floors[0] = squareFloorWithWalls();
    return p;
}

// A door on a wall, 0.9 by 2.1, centred at an offset.
inline Opening doorOn(const std::string& wall, double offset)
{
    Opening o;
    o.wall = wall;
    o.offset = offset;
    o.type = OpeningType::Door;
    o.width = 0.9;
    o.height = 2.1;
    return o;
}

}
