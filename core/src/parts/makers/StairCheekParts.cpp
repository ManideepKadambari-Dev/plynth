#include "plynth/core/BuildParts.h"
#include "plynth/core/Decks.h"
#include "plynth/core/Stairs.h"

#include <cmath>
#include <numbers>

namespace plynth {

namespace {

// Where a point of the stair's own frame (u across, v along the climb, z up) is in the world.
struct StairFrame {
    Vec2 origin;
    Vec2 climb;
    Vec2 right;
    double level = 0.0;

    // The world point for frame point (u, v, z), z above the floor's level.
    Vec3 at(double u, double v, double z) const
    {
        const Vec2 plan{origin.x + v * climb.x + u * right.x, origin.y + v * climb.y + u * right.y};
        const Vec2 world = planToWorld(plan);
        return {world.x, world.y, level + z};
    }
};

// The frame of a stair, as stairLayout places its steps.
StairFrame frameOf(const Stair& stair, double level)
{
    const double r = stair.rotation * std::numbers::pi / 180.0;
    StairFrame frame;
    frame.origin = {stair.x, stair.y};
    frame.climb = {-std::sin(r), std::cos(r)};
    frame.right = rightOf(frame.climb);
    frame.level = level;
    return frame;
}

// Adds one cheek: a solid from u0 to u1 across and 0 to length along, from the ground up to front at its front
// and back at its back. Each face is anticlockwise seen from outside, since the frame to world change keeps
// handedness.
void addCheek(BuildPart& part, const StairFrame& f, double u0, double u1, double length, double front, double back,
              const std::string& slot)
{
    const Vec3 b0 = f.at(u0, 0.0, 0.0);
    const Vec3 b1 = f.at(u1, 0.0, 0.0);
    const Vec3 b2 = f.at(u1, length, 0.0);
    const Vec3 b3 = f.at(u0, length, 0.0);
    const Vec3 t0 = f.at(u0, 0.0, front);
    const Vec3 t1 = f.at(u1, 0.0, front);
    const Vec3 t2 = f.at(u1, length, back);
    const Vec3 t3 = f.at(u0, length, back);
    part.faces.push_back({{b0, b3, b2, b1}, {}, slot});
    part.faces.push_back({{t0, t1, t2, t3}, {}, slot});
    part.faces.push_back({{b0, b1, t1, t0}, {}, slot});
    part.faces.push_back({{b2, b3, t3, t2}, {}, slot});
    part.faces.push_back({{b0, t0, t3, b3}, {}, slot});
    part.faces.push_back({{b1, b2, t2, t1}, {}, slot});
    part.hulls.push_back({b0, b1, b2, b3, t0, t1, t2, t3});
}

// Makes the solid side walls along both sides of every straight stair that asks for cheeks.
class StairCheekMaker final : public PartMaker {
public:
    std::string_view id() const override { return "stair_cheeks"; }
    int order() const override { return 25; }
    std::vector<std::string> kinds() const override { return {"stair"}; }
    std::string_view exportGroup() const override { return "surfaces"; }

    // The cheeks of the stairs of one floor.
    PartOutput floorParts(const PartInput& input) const override
    {
        PartOutput out;
        if (!input.floor || !input.project) return out;
        const Floor& floor = *input.floor;
        const double level = input.levels && input.floorIndex < input.levels->size() ? (*input.levels)[input.floorIndex] : 0.0;
        for (const Stair& stair : floor.stairs) {
            if (!stair.cheeks) continue;
            const StairLayout layout = stairLayout(input.project->plan, floor, stair);
            if (!layout.status.ok() || layout.riserCount < 2) continue;
            if (stair.type != kStraightStairId) {
                out.problems.push_back({"stair_cheeks_not_straight", BuildSeverity::Warning,
                                        "Stair " + stair.id + " gets no side cheeks, since they are built only along a straight flight.",
                                        floor.id, stair.id, std::string(kCheckStep)});
                continue;
            }
            BuildPart part;
            part.object = floor.id + "_" + stair.id + "_Cheeks";
            part.kind = "stair";
            part.id = stair.id;
            const std::string slot = partSlot(*input.project, Surface::Stair, stair.material).slot;
            const StairFrame frame = frameOf(stair, level);
            const double half = stair.width / 2.0;
            const double length = (layout.riserCount - 1) * layout.going;
            const double front = layout.riser + kCheekRise;
            const double back = layout.rise + kCheekRise;
            addCheek(part, frame, -half - kCheekThickness, -half, length, front, back, slot);
            addCheek(part, frame, half, half + kCheekThickness, length, front, back, slot);
            out.parts.push_back(std::move(part));
        }
        return out;
    }
};

const PartMakerRegistrar registrar(std::make_unique<StairCheekMaker>());

}

}
