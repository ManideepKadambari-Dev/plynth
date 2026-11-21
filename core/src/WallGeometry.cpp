#include "plynth/core/WallGeometry.h"

namespace plynth {

// The wall's own height when set, otherwise the floor's ceiling height.
double wallHeight(const Floor& floor, const Wall& wall)
{
    return wall.height ? *wall.height : floor.ceilingHeight;
}

}
