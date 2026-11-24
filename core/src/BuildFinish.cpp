#include "plynth/core/BuildFinish.h"

namespace plynth {

// The power of a light of a kind.
double lightPower(LightKind kind)
{
    switch (kind) {
    case LightKind::Ceiling: return kCeilingLightPower;
    case LightKind::Wall: return kWallLightPower;
    case LightKind::Standing: return kStandingLightPower;
    }
    return kCeilingLightPower;
}

// The lights of a floor at a level.
std::vector<LightBuild> floorLights(const Floor& floor, double elevation)
{
    std::vector<LightBuild> out;
    for (const LightMarker& marker : floor.lights) {
        const Vec2 world = planToWorld({marker.x, marker.y});
        out.push_back({marker.id, marker.kind, {world.x, world.y, elevation + marker.z}, lightPower(marker.kind)});
    }
    return out;
}

// The object name of a floor's trim of a kind.
std::string trimObjectName(const Id& floor, TrimKind kind)
{
    std::string name(trimKindName(kind));
    if (!name.empty()) name[0] = static_cast<char>(name[0] - 'a' + 'A');
    return "SM_" + floor + "_" + name;
}

}
