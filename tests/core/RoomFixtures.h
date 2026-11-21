#pragma once

#include <plynth/core/Plan.h>
#include <plynth/core/Project.h>

#include <string>
#include <vector>

namespace plynth::test {

// Adds a point to the floor.
inline void addPoint(Floor& f, const std::string& id, double x, double y)
{
    f.points.push_back({id, x, y, 0.0});
}

// Adds a wall between two points of the floor.
inline void addWall(Floor& f, const std::string& id, const std::string& from, const std::string& to, double thickness)
{
    Wall w;
    w.id = id;
    w.from = from;
    w.to = to;
    w.thickness = thickness;
    f.walls.push_back(w);
}

// Adds a closed rectangle of four points and walls with ids made from the prefix number n: points p<n>1 to
// p<n>4 and walls w<n>1 to w<n>4, running clockwise on screen from the top left corner.
inline void addRectangle(Floor& f, int n, double x0, double y0, double x1, double y1, double thickness)
{
    const std::string s = std::to_string(n);
    addPoint(f, "p" + s + "1", x0, y0);
    addPoint(f, "p" + s + "2", x1, y0);
    addPoint(f, "p" + s + "3", x1, y1);
    addPoint(f, "p" + s + "4", x0, y1);
    addWall(f, "w" + s + "1", "p" + s + "1", "p" + s + "2", thickness);
    addWall(f, "w" + s + "2", "p" + s + "2", "p" + s + "3", thickness);
    addWall(f, "w" + s + "3", "p" + s + "3", "p" + s + "4", thickness);
    addWall(f, "w" + s + "4", "p" + s + "4", "p" + s + "1", thickness);
}

// A project whose first floor holds a 6 by 4 rectangle of 0.3 m walls split by a wall at x 3 into two areas.
// Points p1 to p6, walls w1 to w7 (w7 is the wall across).
inline Project splitHouseProject()
{
    Project p = makeNewProject();
    Floor& f = p.plan.floors[0];
    addPoint(f, "p1", 0, 0);
    addPoint(f, "p2", 6, 0);
    addPoint(f, "p3", 6, 4);
    addPoint(f, "p4", 0, 4);
    addPoint(f, "p5", 3, 0);
    addPoint(f, "p6", 3, 4);
    addWall(f, "w1", "p1", "p5", 0.3);
    addWall(f, "w2", "p5", "p2", 0.3);
    addWall(f, "w3", "p2", "p3", 0.3);
    addWall(f, "w4", "p3", "p6", 0.3);
    addWall(f, "w5", "p6", "p4", 0.3);
    addWall(f, "w6", "p4", "p1", 0.3);
    addWall(f, "w7", "p5", "p6", 0.3);
    return p;
}

// A project whose first floor holds a 3 by 4 room of 0.3 m walls (points p1 to p4, walls w1 to w4) with an
// unconnected 1 by 1 island of 0.2 m walls inside it (points p5 to p8, walls w5 to w8).
inline Project islandProject()
{
    Project p = makeNewProject();
    Floor& f = p.plan.floors[0];
    addPoint(f, "p1", 0, 0);
    addPoint(f, "p2", 3, 0);
    addPoint(f, "p3", 3, 4);
    addPoint(f, "p4", 0, 4);
    addWall(f, "w1", "p1", "p2", 0.3);
    addWall(f, "w2", "p2", "p3", 0.3);
    addWall(f, "w3", "p3", "p4", 0.3);
    addWall(f, "w4", "p4", "p1", 0.3);
    addPoint(f, "p5", 1, 1);
    addPoint(f, "p6", 2, 1);
    addPoint(f, "p7", 2, 2);
    addPoint(f, "p8", 1, 2);
    addWall(f, "w5", "p5", "p6", 0.2);
    addWall(f, "w6", "p6", "p7", 0.2);
    addWall(f, "w7", "p7", "p8", 0.2);
    addWall(f, "w8", "p8", "p5", 0.2);
    return p;
}

}
