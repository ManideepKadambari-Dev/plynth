#include "plynth/core/LightStrips.h"

#include "plynth/core/Geometry.h"
#include "plynth/core/Stairs.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace plynth {

namespace {

constexpr std::string_view kStripSlot = "Light_Strip";
constexpr double kSamePoint = 1e-6;

// The sum of two vectors.
Vec2 plus(Vec2 a, Vec2 b)
{
    return {a.x + b.x, a.y + b.y};
}

// The first vector less the second.
Vec2 minus(Vec2 a, Vec2 b)
{
    return {a.x - b.x, a.y - b.y};
}

// A vector made k times as long.
Vec2 times(Vec2 a, double k)
{
    return {a.x * k, a.y * k};
}

// A point in the world at a height.
Vec3 at(Vec2 p, double z)
{
    return {p.x, p.y, z};
}

// The ring turned round when needed so it goes anticlockwise seen from above.
std::vector<Vec2> anticlockwise(std::vector<Vec2> ring)
{
    if (signedArea(ring) < 0.0) std::reverse(ring.begin(), ring.end());
    return ring;
}

// Adds the lights and housing of a cove, soffit or edge strip, one piece for each pair of points in a row.
void addLinePieces(const LightStrip& strip, double level, BuildPart& part, PartOutput& out)
{
    std::vector<Vec2> points;
    for (const PlanXY& p : strip.points) points.push_back(planToWorld({p.x, p.y}));
    const double z = level + strip.z;
    const double half = kStripWidth / 2.0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const Vec2 a = points[i - 1];
        const Vec2 b = points[i];
        const double length = distance(a, b);
        if (length < kSamePoint) continue;
        const Vec2 d = times(minus(b, a), 1.0 / length);
        // To the right of the line as the plan shows it.
        const Vec2 right{d.y, -d.x};
        // Inner corners are filled by running each housing past them.
        const double reach = strip.kind == LightStripKind::Edge ? kStripDepth : half;
        const Vec2 a2 = i > 1 ? minus(a, times(d, reach)) : a;
        const Vec2 b2 = i + 1 < points.size() ? plus(b, times(d, reach)) : b;

        StripLight light;
        light.id = strip.id;
        light.width = kStripWidth;
        light.power = strip.power * length;
        PartPrism housing;
        housing.slot = std::string(kStripSlot);
        switch (strip.kind) {
        case LightStripKind::Soffit:
            light.start = at(a, z - kStripGap);
            light.end = at(b, z - kStripGap);
            light.facing = {0.0, 0.0, -1.0};
            housing.outline = anticlockwise({plus(a2, times(right, half)), plus(b2, times(right, half)),
                                             minus(b2, times(right, half)), minus(a2, times(right, half))});
            housing.bottom = z;
            housing.top = z + kStripDepth;
            break;
        case LightStripKind::Edge:
            light.start = at(plus(a, times(right, kStripGap)), z);
            light.end = at(plus(b, times(right, kStripGap)), z);
            light.facing = {right.x, right.y, 0.0};
            housing.outline = anticlockwise({a2, b2, minus(b2, times(right, kStripDepth)), minus(a2, times(right, kStripDepth))});
            housing.bottom = z - half;
            housing.top = z + half;
            break;
        default:
            light.start = at(a, z + kStripGap);
            light.end = at(b, z + kStripGap);
            light.facing = {0.0, 0.0, 1.0};
            housing.outline = anticlockwise({plus(a2, times(right, half)), plus(b2, times(right, half)),
                                             minus(b2, times(right, half)), minus(a2, times(right, half))});
            housing.bottom = z - kStripDepth;
            housing.top = z;
            break;
        }
        out.lights.push_back(light);
        part.prisms.push_back(std::move(housing));
    }
}

// Adds the lights and housing under the nose of every step of a step strip's stair, or the problem when the
// stair is gone.
void addStepPieces(const PartInput& input, const LightStrip& strip, double level, BuildPart& part, PartOutput& out)
{
    const Floor& floor = *input.floor;
    const Stair* stair = strip.stair ? findStair(floor, *strip.stair) : nullptr;
    if (!stair) {
        BuildProblem problem;
        problem.code = "light_strip_stair_missing";
        problem.severity = BuildSeverity::Error;
        problem.message = strip.stair ? "The step light strip " + strip.id + " names the stair " + *strip.stair +
                                            ", which is not on its floor, so it is left out."
                                      : "The step light strip " + strip.id + " names no stair, so it is left out.";
        problem.floor = floor.id;
        problem.object = strip.id;
        problem.step = std::string(kCheckStep);
        out.problems.push_back(std::move(problem));
        return;
    }
    const StairLayout layout = stairLayout(input.project->plan, floor, *stair);
    if (!layout.status.ok()) return;
    const double half = kStripWidth / 2.0;
    for (const StairStep& step : layout.steps) {
        if (step.part != StairPart::Step) continue;
        const std::optional<StepNose> nose = stepNose(step.outline, layout.walkLine);
        if (!nose) continue;
        const double length = distance(nose->from, nose->to);
        const double lit = length - 2.0 * kStepStripInset;
        if (lit < kSamePoint) continue;
        const Vec2 along = times(minus(nose->to, nose->from), 1.0 / length);
        const Vec2 p = planToWorld(plus(nose->from, times(along, kStepStripInset)));
        const Vec2 q = planToWorld(minus(nose->to, times(along, kStepStripInset)));
        const Vec2 outward{nose->out.x, -nose->out.y};
        const double z = level + step.top - kStepStripDrop;

        StripLight light;
        light.id = strip.id;
        light.width = kStripWidth;
        light.power = strip.power * lit;
        light.start = at(plus(p, times(outward, kStripDepth + kStripGap)), z);
        light.end = at(plus(q, times(outward, kStripDepth + kStripGap)), z);
        light.facing = {outward.x / std::sqrt(2.0), outward.y / std::sqrt(2.0), -1.0 / std::sqrt(2.0)};
        out.lights.push_back(light);

        PartPrism housing;
        housing.slot = std::string(kStripSlot);
        housing.outline = anticlockwise({p, q, plus(q, times(outward, kStripDepth)), plus(p, times(outward, kStripDepth))});
        housing.bottom = z - half;
        housing.top = z + half;
        part.prisms.push_back(std::move(housing));
    }
}

// Makes the light strips of every floor: an area light along each straight piece and one housing part per strip.
class LightStripMaker final : public PartMaker {
public:
    std::string_view id() const override { return "light_strips"; }
    int order() const override { return 80; }
    std::vector<std::string> kinds() const override { return {"light"}; }
    std::string_view exportGroup() const override { return "surfaces"; }

    // The lights and housings of one floor's strips, in the floor's strip order.
    PartOutput floorParts(const PartInput& input) const override
    {
        PartOutput out;
        if (!input.floor || !input.project || !input.levels || input.floorIndex >= input.levels->size()) return out;
        const double level = (*input.levels)[input.floorIndex];
        for (const LightStrip& strip : input.floor->lightStrips) {
            BuildPart part;
            part.object = input.floor->id + "_" + strip.id + "_Strip";
            part.kind = "light";
            part.id = strip.id;
            part.bevel = false;
            part.uv = PartUv::Along;
            if (strip.kind == LightStripKind::Step) {
                addStepPieces(input, strip, level, part, out);
            } else {
                addLinePieces(strip, level, part, out);
            }
            if (!part.prisms.empty()) out.parts.push_back(std::move(part));
        }
        return out;
    }
};

const PartMakerRegistrar registrar(std::make_unique<LightStripMaker>());

}

}
