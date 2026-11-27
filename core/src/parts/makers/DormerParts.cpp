#include "plynth/core/BuildParts.h"
#include "plynth/core/Dormers.h"
#include "plynth/core/Roofs.h"
#include "parts/DormerShape.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace plynth {

namespace {

using dormers::Shape;
using Height = std::function<double(Vec2)>;

// How deep the window frame is across the front wall, metres.
constexpr double kFrameDepth = 0.07;
// How thick the pane is.
constexpr double kPaneThickness = 0.01;
// How far the roof cut reaches below and above the dormer, so no cut lies on a face.
constexpr double kCutMargin = 0.05;
// The front wall's inner foot stays this far below the window's sill.
constexpr double kSillClearance = 0.02;

// A local point of the dormer at a height, in world coordinates.
Vec3 lifted(const Shape& shape, Vec2 local, double z)
{
    const Vec2 p = shape.at(local);
    return {p.x, p.y, z};
}

// Adds a solid standing on a convex local ring between two heights that change evenly over it: its top, its
// bottom and one upright face on each side.
void addColumn(std::vector<PartFace>& faces, const Shape& shape, const std::vector<Vec2>& ring, const Height& bottom,
               const Height& top, const std::string& slot)
{
    if (ring.size() < 3 || dormers::ringArea(ring) < 1e-6) return;
    PartFace up;
    up.slot = slot;
    PartFace down;
    down.slot = slot;
    for (const Vec2& p : ring) up.outline.push_back(lifted(shape, p, top(p)));
    for (auto it = ring.rbegin(); it != ring.rend(); ++it) down.outline.push_back(lifted(shape, *it, bottom(*it)));
    faces.push_back(std::move(up));
    faces.push_back(std::move(down));
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec2 a = ring[i];
        const Vec2 b = ring[(i + 1) % ring.size()];
        PartFace side;
        side.slot = slot;
        side.outline = {lifted(shape, a, bottom(a)), lifted(shape, b, bottom(b)), lifted(shape, b, top(b)), lifted(shape, a, top(a))};
        faces.push_back(std::move(side));
    }
}

// One point of the front wall's outline: u across, and its height on the outer and on the inner face.
struct FrontPoint {
    double u = 0.0;
    double outer = 0.0;
    double inner = 0.0;
};

// Adds the upright sides joining two rings of the front wall, one on its outer face and one on its inner face.
void addFrontSides(std::vector<PartFace>& faces, const Shape& shape, const std::vector<FrontPoint>& ring, double thickness,
                   const std::string& slot)
{
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const FrontPoint& a = ring[i];
        const FrontPoint& b = ring[(i + 1) % ring.size()];
        PartFace side;
        side.slot = slot;
        side.outline = {lifted(shape, {a.u, 0.0}, a.outer), lifted(shape, {a.u, thickness}, a.inner),
                        lifted(shape, {b.u, thickness}, b.inner), lifted(shape, {b.u, 0.0}, b.outer)};
        faces.push_back(std::move(side));
    }
}

// Adds the front wall: across the whole front from its outer face back to the wall's thickness, from the main
// roof's underside up to the dormer's own roof, with the window's hole.
void addFront(std::vector<PartFace>& faces, const Shape& shape, const Dormer& dormer, const std::string& slot)
{
    const double t = kDormerWallThickness;
    const double half = dormer.width / 2.0;
    const double sill = shape.footZ + dormer.windowSill;
    const double footOuter = shape.underside({0.0, 0.0});
    const double footInner = std::min(shape.underside({0.0, t}), sill - kSillClearance);
    const auto topAt = [&](double u) { return FrontPoint{u, shape.roofUnder({u, 0.0}), shape.roofUnder({u, t})}; };

    std::vector<FrontPoint> outline{{-half, footOuter, footInner}, {half, footOuter, footInner}, topAt(half)};
    if (shape.kind == dormers::Kind::Gable) outline.push_back(topAt(0.0));
    outline.push_back(topAt(-half));
    const double w = dormer.windowWidth / 2.0;
    const double head = sill + dormer.windowHeight;
    const std::vector<FrontPoint> hole{{-w, sill, sill}, {-w, head, head}, {w, head, head}, {w, sill, sill}};

    PartFace outer;
    outer.slot = slot;
    for (const FrontPoint& p : outline) outer.outline.push_back(lifted(shape, {p.u, 0.0}, p.outer));
    outer.holes.emplace_back();
    for (const FrontPoint& p : hole) outer.holes.back().push_back(lifted(shape, {p.u, 0.0}, p.outer));
    PartFace inner;
    inner.slot = slot;
    for (auto it = outline.rbegin(); it != outline.rend(); ++it) inner.outline.push_back(lifted(shape, {it->u, t}, it->inner));
    inner.holes.emplace_back();
    for (auto it = hole.rbegin(); it != hole.rend(); ++it) inner.holes.back().push_back(lifted(shape, {it->u, t}, it->inner));
    faces.push_back(std::move(outer));
    faces.push_back(std::move(inner));
    addFrontSides(faces, shape, outline, t, slot);
    addFrontSides(faces, shape, hole, t, slot);
}

// Adds an upright box: u0 to u1 across, t0 to t1 back, z0 to z1 high.
void addBox(std::vector<PartPrism>& prisms, const Shape& shape, double u0, double u1, double t0, double t1, double z0,
            double z1, const std::string& slot)
{
    PartPrism prism;
    prism.outline = shape.atRing({{u0, t0}, {u1, t0}, {u1, t1}, {u0, t1}});
    prism.bottom = z0;
    prism.top = z1;
    prism.slot = slot;
    prisms.push_back(std::move(prism));
}

// Adds the window: a frame round its hole in the middle of the front wall, and the pane inside the frame.
void addWindow(std::vector<PartPrism>& prisms, const Shape& shape, const Dormer& dormer, const std::string& frame,
               const std::string& glass)
{
    const double f = kOpeningFrameWidth;
    const double w = dormer.windowWidth / 2.0;
    const double sill = shape.footZ + dormer.windowSill;
    const double head = sill + dormer.windowHeight;
    const double middle = kDormerWallThickness / 2.0;
    const double t0 = middle - kFrameDepth / 2.0;
    const double t1 = middle + kFrameDepth / 2.0;
    addBox(prisms, shape, -w, -w + f, t0, t1, sill, head, frame);
    addBox(prisms, shape, w - f, w, t0, t1, sill, head, frame);
    addBox(prisms, shape, -w + f, w - f, t0, t1, sill, sill + f, frame);
    addBox(prisms, shape, -w + f, w - f, t0, t1, head - f, head, frame);
    addBox(prisms, shape, -w + f, w - f, middle - kPaneThickness / 2.0, middle + kPaneThickness / 2.0, sill + f, head - f,
           glass);
}

// The points of a local ring at two heights, for a collision hull.
std::vector<Vec3> hullOf(const Shape& shape, const std::vector<Vec2>& ring, const Height& bottom, const Height& top)
{
    std::vector<Vec3> out;
    for (const Vec2& p : ring) {
        out.push_back(lifted(shape, p, bottom(p)));
        out.push_back(lifted(shape, p, top(p)));
    }
    return out;
}

// The object name of a dormer: <floor>_<dormer>_Dormer, with any character an object name cannot have as "_".
std::string objectName(const Floor& floor, const Dormer& dormer)
{
    std::string name = floor.id + "_" + dormer.id + "_Dormer";
    std::replace(name.begin(), name.end(), '-', '_');
    return name;
}

// The part of one dormer that fits its roof.
BuildPart dormerPart(const Project& project, const Floor& floor, const Dormer& dormer, const Shape& shape)
{
    const std::string wall = partSlot(project, Surface::WallOutside, dormer.material).slot;
    const std::string roof = partSlot(project, Surface::Roof, dormer.roofMaterial).slot;
    const std::string frame = partSlot(project, Surface::Frame, std::nullopt).slot;
    const std::string glass = partSlot(project, Surface::Glass, std::nullopt).slot;
    const Height underside = [&](Vec2 q) { return shape.underside(q); };
    const Height roofUnder = [&](Vec2 q) { return shape.roofUnder(q); };
    const Height roofTop = [&](Vec2 q) { return shape.roofUnder(q) + kRoofThickness; };

    BuildPart part;
    part.object = objectName(floor, dormer);
    part.kind = "dormer";
    part.id = dormer.id;
    addFront(part.faces, shape, dormer, wall);

    // The cheeks stand behind the front wall and run back to where their top meets the main roof.
    const double t = kDormerWallThickness;
    const double half = dormer.width / 2.0;
    const double far = shape.depth + 1.0;
    const auto standing = [&](Vec2 q) { return shape.roofUnder(q) - shape.covering(q); };
    for (const double side : {-1.0, 1.0}) {
        const double u0 = side < 0.0 ? -half : half - t;
        const double u1 = side < 0.0 ? -half + t : half;
        const std::vector<Vec2> cheek = dormers::clippedRing({{u0, t}, {u1, t}, {u1, far}, {u0, far}}, standing);
        addColumn(part.faces, shape, cheek, underside, roofUnder, wall);
    }
    for (const std::vector<Vec2>& slab : shape.slabs) addColumn(part.faces, shape, slab, roofUnder, roofTop, roof);
    addWindow(part.prisms, shape, dormer, frame, glass);

    std::vector<Vec2> box = shape.footprint;
    if (shape.kind == dormers::Kind::Gable) box.push_back({0.0, 0.0});
    part.hulls.push_back(hullOf(shape, box, underside, roofUnder));
    for (const std::vector<Vec2>& slab : shape.slabs) {
        if (slab.size() >= 3) part.hulls.push_back(hullOf(shape, slab, roofUnder, roofTop));
    }
    return part;
}

// The cut a dormer makes in the roof section it stands on.
RoofCut dormerCut(const Floor& floor, const Shape& shape)
{
    RoofCut cut;
    cut.floor = floor.id;
    cut.index = shape.roofIndex;
    cut.cutter.outline = shape.atRing(shape.footprint);
    cut.cutter.bottom = shape.underside({0.0, 0.0}) - kCutMargin;
    cut.cutter.top = shape.covering({0.0, shape.depth}) + kCutMargin;
    return cut;
}

// A problem with a dormer that is left out of the build.
BuildProblem leftOut(const std::string& code, const std::string& message, const Floor& floor, const Dormer& dormer)
{
    BuildProblem problem;
    problem.code = code;
    problem.severity = BuildSeverity::Error;
    problem.message = message;
    problem.floor = floor.id;
    problem.object = dormer.id;
    return problem;
}

// Dormers (Dormers.h): each one that fits its pitched roof becomes one part and a cut in that roof.
class DormerMaker final : public PartMaker {
public:
    std::string_view id() const override { return "dormers"; }
    int order() const override { return 50; }
    std::vector<std::string> kinds() const override { return {"dormer"}; }
    std::string_view exportGroup() const override { return "surfaces"; }

    // The dormers of one floor, in plan order; a later one that overlaps an earlier one is left out.
    PartOutput floorParts(const PartInput& input) const override
    {
        PartOutput out;
        if (!input.floor || input.floor->dormers.empty()) return out;
        const Floor& floor = *input.floor;
        static const std::vector<RoofBuild> noRoofs;
        const std::vector<RoofBuild>* roofs = &noRoofs;
        if (input.floors) {
            for (const FloorBuild& build : *input.floors) {
                if (build.id == floor.id) roofs = &build.roofs;
            }
        }
        const std::vector<RoofArea> areas = roofAreasOf(input.project->plan, floor.id);

        std::vector<std::pair<Id, Shape>> placed;
        for (const Dormer& dormer : floor.dormers) {
            const Shape shape = dormers::shapeOf(dormer, floor.id, *roofs, areas);
            if (!shape.code.empty()) {
                out.problems.push_back(leftOut(shape.code, shape.status.message, floor, dormer));
                continue;
            }
            const auto clash = std::find_if(placed.begin(), placed.end(),
                                            [&](const auto& other) { return dormers::overlaps(shape, other.second); });
            if (clash != placed.end()) {
                out.problems.push_back(leftOut("dormer_overlap",
                                               "Dormer " + dormer.id + " overlaps dormer " + clash->first + ", so it is left out.",
                                               floor, dormer));
                continue;
            }
            out.parts.push_back(dormerPart(*input.project, floor, dormer, shape));
            out.roofCuts.push_back(dormerCut(floor, shape));
            placed.emplace_back(dormer.id, shape);
        }
        return out;
    }
};

const PartMakerRegistrar registrar(std::make_unique<DormerMaker>());

}

}
