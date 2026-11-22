#pragma once

#include "SquareFixtures.h"

#include <plynth/core/Rooms.h>

#include <optional>
#include <string>
#include <utility>

namespace plynth::test {

// A 4 by 4 m square split by the inner wall w7 (x = 2) into r1 on the left and r2 on the right. The outer
// walls are w1 and w2 on the top, w3 on the right, w4 and w5 on the bottom, w6 on the left; each runs
// clockwise on the plan, so the right side of every outer wall looks into the room. A door o1 is in w7 with
// its swing toward the right side of the wall, which is r1.
inline Project splitRoomProject()
{
    Project project = makeNewProject();
    Floor f;
    f.id = "f1";
    f.name = "Floor 1";
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 2.0, 0.0, 0.0}, {"p3", 4.0, 0.0, 0.0},
                {"p4", 4.0, 4.0, 0.0}, {"p5", 2.0, 4.0, 0.0}, {"p6", 0.0, 4.0, 0.0}};
    const char* ends[7][2] = {{"p1", "p2"}, {"p2", "p3"}, {"p3", "p4"}, {"p4", "p5"}, {"p5", "p6"}, {"p6", "p1"}, {"p2", "p5"}};
    for (int i = 0; i < 7; ++i) {
        Wall w;
        w.id = "w" + std::to_string(i + 1);
        w.from = ends[i][0];
        w.to = ends[i][1];
        w.thickness = 0.2;
        f.walls.push_back(w);
    }
    const std::pair<const char*, Vec2> spots[2] = {{"r1", {1.0, 2.0}}, {"r2", {3.0, 2.0}}};
    for (const auto& [id, at] : spots) {
        const std::optional<ClosedArea> area = findClosedAreaAt(f, at);
        Room r;
        r.id = id;
        r.walls = area->walls;
        f.rooms.push_back(r);
    }
    Opening door = doorOn("w7", 2.0);
    door.id = "o1";
    door.swing = WallSide::Right;
    f.openings.push_back(door);
    project.plan.floors[0] = f;
    return project;
}

}
