#include "plynth/core/BuildParts.h"
#include "plynth/core/Railings.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace plynth {

namespace {

constexpr double kSharpestMitre = 0.2;      // cosine of half the turn; sharper corners stay square
constexpr double kAtRunEnd = 1e-7;          // metres; a point this near u = 0 or u = length is at the end
constexpr double kMinCollisionHalf = 0.02;  // metres; half the width of the thinnest collision box

// A run of the railing in the world: where it starts, which way it goes, and the mitres at its two ends.
struct RunFrame {
    Vec2 origin;
    Vec2 along{1.0, 0.0};
    Vec2 right{0.0, -1.0};
    double length = 0.0;
    double foot = 0.0;
    std::optional<Vec2> startMitre;
    std::optional<Vec2> endMitre;
};

// The unit direction of a run seen from above, or empty for a run with no length.
std::optional<Vec2> directionOf(const RailingRun& run)
{
    const double dx = run.end.x - run.start.x;
    const double dy = run.end.y - run.start.y;
    const double length = std::hypot(dx, dy);
    if (length < 1e-9) return std::nullopt;
    return Vec2{dx / length, dy / length};
}

// The direction across the mitre line where a run going in meets a run going out, or empty when the turn is
// too sharp to mitre.
std::optional<Vec2> mitreNormal(Vec2 in, Vec2 out)
{
    Vec2 n{in.x + out.x, in.y + out.y};
    const double length = std::hypot(n.x, n.y);
    if (length < 1e-9) return std::nullopt;
    n = {n.x / length, n.y / length};
    if (in.x * n.x + in.y * n.y < kSharpestMitre) return std::nullopt;
    return n;
}

// Moves a point along the run until it lies on the mitre line through a corner.
Vec2 ontoMitre(Vec2 p, Vec2 corner, Vec2 along, Vec2 normal)
{
    const double t = -((p.x - corner.x) * normal.x + (p.y - corner.y) * normal.y) / (along.x * normal.x + along.y * normal.y);
    return {p.x + along.x * t, p.y + along.y * t};
}

// A point of the run's frame in world X and Y, mitred where it lies at an end that meets another run.
Vec2 place(const RunFrame& f, double u, double v)
{
    const Vec2 p{f.origin.x + f.along.x * u + f.right.x * v, f.origin.y + f.along.y * u + f.right.y * v};
    if (f.startMitre && std::fabs(u) <= kAtRunEnd) return ontoMitre(p, f.origin, f.along, *f.startMitre);
    if (f.endMitre && std::fabs(u - f.length) <= kAtRunEnd) {
        const Vec2 corner{f.origin.x + f.along.x * f.length, f.origin.y + f.along.y * f.length};
        return ontoMitre(p, corner, f.along, *f.endMitre);
    }
    return p;
}

// A ring of the run's frame in the world, turned the way it is asked to turn.
std::vector<Vec2> placeRing(const RunFrame& f, const std::vector<Vec2>& ring, bool anticlockwise)
{
    std::vector<Vec2> out;
    for (const Vec2& p : ring) out.push_back(place(f, p.x, p.y));
    if ((signedArea(out) > 0.0) != anticlockwise) std::reverse(out.begin(), out.end());
    return out;
}

// A prism of the run's frame in the world, with its slot.
PartPrism placePrism(const RunFrame& f, const PartPrism& prism, const std::string& slot)
{
    PartPrism out;
    out.outline = placeRing(f, prism.outline, true);
    for (const std::vector<Vec2>& hole : prism.holes) out.holes.push_back(placeRing(f, hole, false));
    out.bottom = f.foot + prism.bottom;
    out.top = f.foot + prism.top;
    out.slot = slot;
    return out;
}

// A sweep of the run's frame in the world, with its slot. The frame is a mirror image of the world's, so every
// section is turned round to keep its faces looking out.
PartSweep placeSweep(const RunFrame& f, const PartSweep& sweep, const std::string& slot)
{
    PartSweep out;
    out.sweep.closed = sweep.sweep.closed;
    for (const std::vector<Vec3>& section : sweep.sweep.sections) {
        std::vector<Vec3> placed;
        for (auto it = section.rbegin(); it != section.rend(); ++it) {
            const Vec2 p = place(f, it->x, it->y);
            placed.push_back({p.x, p.y, f.foot + it->z});
        }
        out.sweep.sections.push_back(std::move(placed));
    }
    out.slot = slot;
    return out;
}

// How far the widest piece of a run reaches across its line.
double halfWidthOf(const std::vector<RailingPiece>& pieces)
{
    double half = kMinCollisionHalf;
    for (const RailingPiece& piece : pieces) {
        for (const PartPrism& prism : piece.prisms) {
            for (const Vec2& p : prism.outline) half = std::max(half, std::fabs(p.y));
        }
        for (const PartSweep& sweep : piece.sweeps) {
            for (const auto& section : sweep.sweep.sections) {
                for (const Vec3& p : section) half = std::max(half, std::fabs(p.y));
            }
        }
    }
    return half;
}

// A problem of a railing found before the build starts.
BuildProblem railingProblem(const Floor& floor, const Railing& railing, std::string code, std::string message)
{
    return {std::move(code), BuildSeverity::Warning, std::move(message), floor.id, railing.id, std::string(kCheckStep)};
}

// Makes one part per drawn railing: the pieces of its style along every run, and a collision box per run.
class RailingMaker final : public PartMaker {
public:
    std::string_view id() const override { return "railings"; }
    int order() const override { return 30; }
    std::vector<std::string> kinds() const override { return {"railing"}; }
    std::string_view exportGroup() const override { return "surfaces"; }

    // The railings of one floor.
    PartOutput floorParts(const PartInput& input) const override
    {
        PartOutput out;
        if (!input.floor || !input.project) return out;
        const Floor& floor = *input.floor;
        static const std::vector<double> noLevels;
        const std::vector<double>& levels = input.levels ? *input.levels : noLevels;
        for (const Railing& railing : floor.railings) {
            const RailingStyle* style = findRailingStyle(railing.style);
            if (!style) {
                out.problems.push_back(railingProblem(floor, railing, "railing_style_unknown",
                                                      "Railing " + railing.id + " uses the railing style " + railing.style +
                                                          ", which is not installed, so it is built with posts and rails."));
                style = findRailingStyle("posts_rails");
                if (!style) continue;
            }
            if (railing.host && !findDeck(floor, *railing.host)) {
                out.problems.push_back(railingProblem(floor, railing, "railing_host_missing",
                                                      "Railing " + railing.id + " stood on deck " + *railing.host +
                                                          ", which is gone, so it stands on its base height."));
            }
            const std::vector<RailingRun> runs = railingRuns(input.project->plan, floor, railing, levels);
            BuildPart part = railingPart(*input.project, floor, railing, *style, runs);
            if (!part.prisms.empty() || !part.sweeps.empty()) out.parts.push_back(std::move(part));
        }
        return out;
    }

private:
    // The part of one railing from its runs.
    static BuildPart railingPart(const Project& project, const Floor& floor, const Railing& railing, const RailingStyle& style,
                                 const std::vector<RailingRun>& runs)
    {
        BuildPart part;
        part.object = floor.id + "_" + railing.id + "_Railing";
        part.kind = "railing";
        part.id = railing.id;
        part.bevel = false; // cables and glass are thinner than the build's bevel
        if (runs.empty()) return part;
        const std::string railSlot = partSlot(project, Surface::Railing, railing.material).slot;
        const std::string glassSlot = partSlot(project, Surface::Glass, railing.glassMaterial).slot;

        std::vector<std::optional<Vec2>> directions;
        for (const RailingRun& run : runs) directions.push_back(directionOf(run));
        const auto same = [](const Vec3& a, const Vec3& b) { return std::hypot(a.x - b.x, a.y - b.y) < 1e-6; };
        const bool closed = runs.size() >= 3 && same(runs.back().end, runs.front().start);
        const std::size_t last = runs.size() - 1;

        for (std::size_t i = 0; i < runs.size(); ++i) {
            if (!directions[i]) continue;
            RunFrame frame;
            frame.origin = {runs[i].start.x, runs[i].start.y};
            frame.along = *directions[i];
            frame.right = {frame.along.y, -frame.along.x};
            frame.length = std::hypot(runs[i].end.x - runs[i].start.x, runs[i].end.y - runs[i].start.y);
            frame.foot = runs[i].start.z;
            const bool hasBefore = i > 0 || closed;
            const bool hasAfter = i < last || closed;
            const std::optional<Vec2>& before = directions[i == 0 ? last : i - 1];
            const std::optional<Vec2>& after = directions[i == last ? 0 : i + 1];
            if (hasBefore && before) frame.startMitre = mitreNormal(*before, frame.along);
            if (hasAfter && after) frame.endMitre = mitreNormal(frame.along, *after);

            RailingRunInput runInput;
            runInput.length = frame.length;
            runInput.height = railing.height;
            runInput.postAtStart = true;
            runInput.postAtEnd = !closed && i == last;
            const std::vector<RailingPiece> pieces = style.run(runInput);
            for (const RailingPiece& piece : pieces) {
                const std::string& slot = piece.role == RailingPiece::Role::Glass ? glassSlot : railSlot;
                for (const PartPrism& prism : piece.prisms) {
                    if (prism.outline.size() < 3 || !(prism.top > prism.bottom)) continue;
                    part.prisms.push_back(placePrism(frame, prism, slot));
                }
                for (const PartSweep& sweep : piece.sweeps) {
                    if (sweep.sweep.sections.size() < 2) continue;
                    part.sweeps.push_back(placeSweep(frame, sweep, slot));
                }
            }

            const double half = halfWidthOf(pieces);
            RunFrame square = frame;
            square.startMitre.reset();
            square.endMitre.reset();
            CollisionPiece box;
            box.outline = placeRing(square, {{0.0, -half}, {frame.length, -half}, {frame.length, half}, {0.0, half}}, true);
            box.bottom = frame.foot;
            box.top = frame.foot + railing.height;
            part.collision.push_back(std::move(box));
        }
        return part;
    }
};

const PartMakerRegistrar registrar(std::make_unique<RailingMaker>());

}

}
