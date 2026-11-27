#pragma once

#include "BuildFixtures.h"
#include "RoofShapeChecks.h"

#include <plynth/core/Dormers.h>
#include <plynth/core/Project.h>

#include <string>

namespace plynth::test {

// One floor 8 by 6 m outside under a 45 degree gable whose ridge runs along x at plan y 3, so the roof's base is
// 3.2 and its top 6.2. No material defaults, so every slot has its plain name.
inline Project gableHouse()
{
    Project project = makeNewProject();
    addBoxRoom(project.plan.floors[0], 1, 0.0, 0.0, 8.0, 6.0);
    project.plan.roof.type = "gable";
    project.plan.roof.pitch = 45.0;
    project.plan.roof.overhang = 0.3;
    project.plan.roof.direction = 0.0;
    clearMaterialDefaults(project);
    return project;
}

// A gable dormer on the south slope of gableHouse: its front at plan y 5.5 faces plan +y, 1.6 m wide and 2.2 m
// deep, so its front is 2.2 - 0.8 tan 30 = 1.738 m high and its ridge meets the main roof at plan y 3.3.
inline Dormer gableDormer(const std::string& id = "dm1", double x = 4.0, double y = 5.5)
{
    Dormer d;
    d.id = id;
    d.x = x;
    d.y = y;
    d.type = "gable";
    d.width = 1.6;
    d.depth = 2.2;
    d.pitch = 30.0;
    d.windowWidth = 0.9;
    d.windowHeight = 1.0;
    d.windowSill = 0.2;
    return d;
}

// A shed dormer on the north slope of gableHouse: its front at plan y 0.5 faces plan -y, its roof at 10 degrees,
// so its front is 2.2 (1 - tan 10) = 1.812 m high.
inline Dormer shedDormer(const std::string& id = "dm2", double x = 4.0, double y = 0.5)
{
    Dormer d = gableDormer(id, x, y);
    d.type = "shed";
    d.pitch = 10.0;
    return d;
}

}
