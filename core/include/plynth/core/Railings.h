#pragma once

#include "BuildGeometry.h"
#include "BuildParts.h"
#include "Plan.h"
#include "Status.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// Railings along drawn lines and fences round the site (issues 17, 18 and 22, plan format 8). Railing styles
// are registry entries: one file each in core/src/railings/styles/ with a RailingStyleRegistrar, so a new style
// is one file. A style turns a straight run of a given length and height into posts, rails, infill and glass in
// the run's own frame; core places the runs along the line, and the parts step of the Blender build makes them.
//
// The run's own frame: u along the run from 0 to its length, v across it toward the run's right side seen from
// above, z up from the foot. Prism outlines may turn either way; the maker turns them anticlockwise in the world.
// Runs are the line's segments; a closed line also runs from its last point back to its first, and a line whose
// last point is its first counts as closed. Every point of a piece that lies at u = 0 or u = length where the run
// meets another run is moved along the run onto the mitre line of that corner, so rails, shoes, cables and walls
// meet cleanly (not at turns sharper than about 157 degrees, where they stay square).
// Posts: a run places a post at its start (so every corner gets one), at its end only when it is the last run of
// an open line, and between them evenly, at most kRailingPostSpacing apart.
//
// The built-in styles (sort key in brackets):
//   posts_rails (10)  square posts, a top rail and two lower rails
//   spindles    (20)  wooden spindles: posts, a top and a bottom rail, round spindles at most 0.1 m apart
//   flat_bars   (30)  metal bars: posts, a top rail, a bottom rail and flat vertical bars at most 0.1 m apart
//   glass       (40)  glass panels kGlassThickness thick standing in a base shoe, with a slim top rail; panels
//                     in the glass slot (surface Glass), shoe and rail in the railing slot; at most 1.5 m tall
//   cables      (50)  slim posts, a top rail and horizontal cables at most 0.08 m apart
//   half_wall   (60)  a solid half wall 0.15 m thick with a coping, no posts; at most 1.5 m tall
//
// The part maker "railings" (order 30, kind "railing", group "surfaces"): one part <floor>_<railing>_Railing
// per railing, standing on its host deck's top when host is set, else base above the floor's level. Its collision
// is one thin box per run, the railing's height tall and as wide as the widest piece. Fences of the site use the
// same styles through the "site" maker (Site.h). Problems: railing_style_unknown (warning: posts_rails is used),
// railing_host_missing (warning: the host deck is gone; it stands on base).

inline constexpr double kRailingPostSpacing = 1.5;    // metres; StairRailings.h has its own kMaxPostSpacing
inline constexpr double kGlassThickness = 0.012;      // metres
inline constexpr double kMinDrawnRailingHeight = 0.3; // metres, the plan format's range
inline constexpr double kMaxDrawnRailingHeight = 3.0; // metres
inline constexpr double kMinRailingSegment = 0.1;     // metres, the shortest piece of a railing line
inline constexpr double kMaxRailingLength = 1000.0;   // metres, the whole line

// One piece of a railing run in the run's own frame (u, v, z).
struct RailingPiece {
    enum class Role { Post, Rail, Infill, Glass, Shoe };
    Role role = Role::Post;
    std::vector<PartPrism> prisms;  // in the run's frame, z from the foot; slot left empty, the maker fills it
    std::vector<PartSweep> sweeps;

    bool operator==(const RailingPiece&) const = default;
};

// What a style needs to know about one run.
struct RailingRunInput {
    double length = 0.0;
    double height = 0.0;
    bool postAtStart = true;
    bool postAtEnd = true;
};

class RailingStyle {
public:
    virtual ~RailingStyle() = default;

    // Stable id stored in plan.json, for example "glass".
    virtual std::string_view id() const = 0;
    // Short name for the app, for example "Glass panels".
    virtual std::string_view name() const = 0;
    virtual int sortKey() const = 0;
    // The pieces of one run.
    virtual std::vector<RailingPiece> run(const RailingRunInput& input) const = 0;
    // The heights the style can be built at; checkRailing refuses others. The plan format's range by default.
    virtual double minHeight() const { return kMinDrawnRailingHeight; }
    virtual double maxHeight() const { return kMaxDrawnRailingHeight; }
};

// Adds a style. False, and it is dropped, when its id is taken or not a well formed lower case name.
bool registerRailingStyle(std::unique_ptr<RailingStyle> style);

// Registers a style while the program starts.
struct RailingStyleRegistrar {
    explicit RailingStyleRegistrar(std::unique_ptr<RailingStyle> style);
};

// The style with this id, or null. Every style, by sort key, then id.
const RailingStyle* findRailingStyle(std::string_view id);
std::vector<const RailingStyle*> allRailingStyles();

// Checks the values a railing command would set, with the rules of checkPlan and a registered style:
// points finite, at least two (three when closed), each piece of the line at least kMinRailingSegment long and
// the whole line at most kMaxRailingLength; height within the plan range and the style's own; base from 0 to 20;
// materials well formed; and a style whose run holds glass must also hold posts or a shoe to stand the glass in.
// InvalidValue with a plain sentence. Whether the host deck exists is the commands' check (NotFound).
Status checkRailing(const Railing& railing);

// The railing's line in world coordinates at its foot, for the plan and the parts: one entry per run. Points
// closer than 1e-6 to the one before are dropped. Empty when no run is left.
struct RailingRun {
    Vec3 start;
    Vec3 end;
};
std::vector<RailingRun> railingRuns(const Plan& plan, const Floor& floor, const Railing& railing,
                                    const std::vector<double>& levels);

// The open edges of a deck's outline that no wall band covers, as lines, for the app's "Add railings round
// this deck" and "plynth railing add --around <deck>". An edge within 0.02 m of a wall's outline counts as
// covered. A deck open all round gives one line that ends at its first point (so it counts as closed).
std::vector<std::vector<PlanXY>> openDeckEdges(const Floor& floor, const Deck& deck);

}
