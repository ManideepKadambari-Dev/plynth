#include "plynth/core/Railings.h"

#include "plynth/core/Decks.h"
#include "plynth/core/Geometry.h"
#include "plynth/core/WallGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace plynth {

namespace {

constexpr double kSamePoint = 1e-6;  // metres; closer points count as one
constexpr double kWallTouch = 0.02;  // metres; a deck edge this near a wall's outline runs along it

struct StyleRegistry {
    std::mutex mutex;
    std::vector<std::unique_ptr<RailingStyle>> styles;
};

// The one style registry, made on first use so registrars in any file can use it while the program starts.
StyleRegistry& styleRegistry()
{
    static StyleRegistry instance;
    return instance;
}

// Says whether an id is a lower case name: a letter, then letters, digits or "_".
bool isWellFormedStyleId(std::string_view id)
{
    if (id.empty() || !(id[0] >= 'a' && id[0] <= 'z')) return false;
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

// Says whether text is a name as the plan format takes it: letters, digits, "_" and "-", at most 128 characters.
bool wellFormedName(const std::string& name)
{
    if (name.empty() || name.size() > 128) return false;
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

// Says whether a number is finite and from low to high, allowing for rounding.
bool inRange(double v, double low, double high)
{
    return std::isfinite(v) && v >= low - 1e-9 && v <= high + 1e-9;
}

// A number for a sentence, without trailing zeros.
std::string number(double v)
{
    char text[32];
    std::snprintf(text, sizeof text, "%.3f", v);
    std::string out(text);
    while (!out.empty() && out.back() == '0') out.pop_back();
    if (!out.empty() && out.back() == '.') out.pop_back();
    return out;
}

// Says whether two plan points are the same place.
bool samePlace(PlanXY a, PlanXY b)
{
    return std::hypot(a.x - b.x, a.y - b.y) < kSamePoint;
}

// A railing's line as it is built: a last point on top of the first closes it.
struct CleanLine {
    std::vector<PlanXY> points;
    bool closed = false;
};

// The line of a railing with the repeated last point taken off and the closing worked out.
CleanLine cleanLine(const std::vector<PlanXY>& points, bool closed)
{
    CleanLine out{points, closed};
    if (out.points.size() >= 3 && samePlace(out.points.front(), out.points.back())) {
        out.points.pop_back();
        out.closed = true;
    }
    return out;
}

// Says whether a style's run holds glass with nothing to stand it in.
bool glassUnheld(const RailingStyle& style, double height)
{
    RailingRunInput probe;
    probe.length = 2.0 * kRailingPostSpacing;
    probe.height = height;
    bool glass = false;
    bool held = false;
    for (const RailingPiece& piece : style.run(probe)) {
        if (piece.prisms.empty() && piece.sweeps.empty()) continue;
        if (piece.role == RailingPiece::Role::Glass) glass = true;
        if (piece.role == RailingPiece::Role::Post || piece.role == RailingPiece::Role::Shoe) held = true;
    }
    return glass && !held;
}

// The 2D cross product of b - a and c - a.
double cross(Vec2 a, Vec2 b, Vec2 c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// Where along the segment from a to b it crosses the segment from p to q, as a share of its length.
std::optional<double> crossingAt(Vec2 a, Vec2 b, Vec2 p, Vec2 q)
{
    const Vec2 r{b.x - a.x, b.y - a.y};
    const Vec2 s{q.x - p.x, q.y - p.y};
    const double denom = r.x * s.y - r.y * s.x;
    if (std::fabs(denom) < 1e-12) return std::nullopt;
    const Vec2 ap{p.x - a.x, p.y - a.y};
    const double t = (ap.x * s.y - ap.y * s.x) / denom;
    const double u = (ap.x * r.y - ap.y * r.x) / denom;
    if (t <= 0.0 || t >= 1.0 || u < -1e-12 || u > 1.0 + 1e-12) return std::nullopt;
    return t;
}

// The distance from a point to the nearest edge of a ring.
double distanceToRing(Vec2 p, const std::vector<Vec2>& ring)
{
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < ring.size(); ++i) {
        best = std::min(best, projectOntoSegment(p, ring[i], ring[(i + 1) % ring.size()]).distance);
    }
    return best;
}

// Says whether a plan point lies in or right next to one of the wall outlines.
bool onWalls(Vec2 p, const std::vector<std::vector<Vec2>>& bands)
{
    for (const std::vector<Vec2>& band : bands) {
        if (insidePolygon(p, band) || distanceToRing(p, band) <= kWallTouch) return true;
    }
    return false;
}

// A line without points that sit on a straight stretch between their neighbours.
std::vector<Vec2> withoutStraightPoints(const std::vector<Vec2>& line)
{
    std::vector<Vec2> out;
    for (const Vec2& p : line) {
        if (out.size() >= 2) {
            const Vec2 a = out[out.size() - 2];
            const Vec2 b = out.back();
            const double along = (b.x - a.x) * (p.x - b.x) + (b.y - a.y) * (p.y - b.y);
            if (std::fabs(cross(a, b, p)) < 1e-12 && along > 0.0) out.back() = p;
            else out.push_back(p);
        } else {
            out.push_back(p);
        }
    }
    return out;
}

}

// Adds a style unless its id is taken or badly formed.
bool registerRailingStyle(std::unique_ptr<RailingStyle> style)
{
    if (!style) return false;
    const std::string id(style->id());
    if (!isWellFormedStyleId(id)) return false;
    StyleRegistry& r = styleRegistry();
    std::lock_guard lock(r.mutex);
    for (const auto& existing : r.styles) {
        if (existing->id() == id) return false;
    }
    r.styles.push_back(std::move(style));
    return true;
}

// Adds the style to the registry when the program starts.
RailingStyleRegistrar::RailingStyleRegistrar(std::unique_ptr<RailingStyle> style)
{
    registerRailingStyle(std::move(style));
}

// Finds a style by its id.
const RailingStyle* findRailingStyle(std::string_view id)
{
    StyleRegistry& r = styleRegistry();
    std::lock_guard lock(r.mutex);
    for (const auto& style : r.styles) {
        if (style->id() == id) return style.get();
    }
    return nullptr;
}

// Lists the styles by sort key, then id.
std::vector<const RailingStyle*> allRailingStyles()
{
    StyleRegistry& r = styleRegistry();
    std::lock_guard lock(r.mutex);
    std::vector<const RailingStyle*> out;
    for (const auto& style : r.styles) out.push_back(style.get());
    std::sort(out.begin(), out.end(), [](const RailingStyle* a, const RailingStyle* b) {
        if (a->sortKey() != b->sortKey()) return a->sortKey() < b->sortKey();
        return a->id() < b->id();
    });
    return out;
}

// Checks the values a railing command would set.
Status checkRailing(const Railing& railing)
{
    for (const PlanXY& p : railing.points) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
            return fail(ErrorCode::InvalidValue, "The railing line points must be finite numbers.");
        }
    }
    if (railing.points.size() < 2) return fail(ErrorCode::InvalidValue, "A railing line needs at least two points.");
    const CleanLine line = cleanLine(railing.points, railing.closed);
    if (line.closed && line.points.size() < 3) {
        return fail(ErrorCode::InvalidValue, "A closed railing line needs at least three points.");
    }
    double total = 0.0;
    const std::size_t runs = line.closed ? line.points.size() : line.points.size() - 1;
    for (std::size_t i = 0; i < runs; ++i) {
        const PlanXY a = line.points[i];
        const PlanXY b = line.points[(i + 1) % line.points.size()];
        const double length = std::hypot(b.x - a.x, b.y - a.y);
        if (length < kMinRailingSegment - 1e-9) {
            return fail(ErrorCode::InvalidValue,
                        "Each piece of a railing line must be at least " + number(kMinRailingSegment) + " m long.");
        }
        total += length;
    }
    if (total > kMaxRailingLength + 1e-9) {
        return fail(ErrorCode::InvalidValue, "A railing line must be at most " + number(kMaxRailingLength) + " m long.");
    }
    if (!inRange(railing.height, kMinDrawnRailingHeight, kMaxDrawnRailingHeight)) {
        return fail(ErrorCode::InvalidValue, "The railing height must be from " + number(kMinDrawnRailingHeight) + " to " +
                                                 number(kMaxDrawnRailingHeight) + " metres.");
    }
    if (!inRange(railing.base, 0.0, 20.0)) return fail(ErrorCode::InvalidValue, "The railing base must be from 0 to 20 metres.");
    if (!wellFormedName(railing.style)) return fail(ErrorCode::InvalidValue, "The railing style is not a valid name.");
    const RailingStyle* style = findRailingStyle(railing.style);
    if (!style) return fail(ErrorCode::InvalidValue, "There is no railing style \"" + railing.style + "\".");
    if (!inRange(railing.height, style->minHeight(), style->maxHeight())) {
        return fail(ErrorCode::InvalidValue, "A railing of " + std::string(style->name()) + " must be from " +
                                                 number(style->minHeight()) + " to " + number(style->maxHeight()) + " m tall.");
    }
    if (railing.material && !wellFormedName(*railing.material)) {
        return fail(ErrorCode::InvalidValue, "The railing material \"" + *railing.material + "\" is not a valid name.");
    }
    if (railing.glassMaterial && !wellFormedName(*railing.glassMaterial)) {
        return fail(ErrorCode::InvalidValue, "The glass material \"" + *railing.glassMaterial + "\" is not a valid name.");
    }
    if (glassUnheld(*style, railing.height)) {
        return fail(ErrorCode::InvalidValue,
                    "The railing style \"" + railing.style + "\" has glass with no posts or base shoe to hold it.");
    }
    return {};
}

// The warnings of one railing: an unknown style and a host deck that is gone.
std::vector<BuildProblem> railingProblems(const Floor& floor, const Railing& railing)
{
    std::vector<BuildProblem> out;
    const auto warn = [&](std::string code, std::string message) {
        out.push_back({std::move(code), BuildSeverity::Warning, std::move(message), floor.id, railing.id, std::string(kCheckStep)});
    };
    if (!findRailingStyle(railing.style)) {
        warn("railing_style_unknown", "Railing " + railing.id + " uses the railing style " + railing.style +
                                          ", which is not installed, so it is built with posts and rails.");
    }
    if (railing.host && !findDeck(floor, *railing.host)) {
        warn("railing_host_missing", "Railing " + railing.id + " stood on deck " + *railing.host +
                                         ", which is gone, so it stands on its base height.");
    }
    return out;
}

// The runs of a railing at its foot: on its host deck's top, or base above the floor's level.
std::vector<RailingRun> railingRuns(const Plan& plan, const Floor& floor, const Railing& railing,
                                    const std::vector<double>& levels)
{
    std::vector<RailingRun> out;
    const auto index = floorIndex(plan, floor.id);
    const double level = index && *index < levels.size() ? levels[*index] : 0.0;
    const Deck* deck = railing.host ? findDeck(floor, *railing.host) : nullptr;
    const double foot = level + (deck ? deck->top : railing.base);

    std::vector<PlanXY> points;
    for (const PlanXY& p : railing.points) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) return out;
        if (points.empty() || !samePlace(points.back(), p)) points.push_back(p);
    }
    const CleanLine line = cleanLine(points, railing.closed);
    const bool closed = line.closed && line.points.size() >= 3;
    const std::size_t n = line.points.size();
    if (n < 2) return out;
    const std::size_t runs = closed ? n : n - 1;
    for (std::size_t i = 0; i < runs; ++i) {
        const PlanXY a = line.points[i];
        const PlanXY b = line.points[(i + 1) % n];
        const Vec2 wa = planToWorld({a.x, a.y});
        const Vec2 wb = planToWorld({b.x, b.y});
        out.push_back({{wa.x, wa.y, foot}, {wb.x, wb.y, foot}});
    }
    return out;
}

// Walks round the deck outline and keeps the stretches no wall outline covers, joined into lines.
std::vector<std::vector<PlanXY>> openDeckEdges(const Floor& floor, const Deck& deck)
{
    std::vector<std::vector<PlanXY>> result;
    std::vector<Vec2> ring;
    for (const PlanXY& p : deck.outline) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) return result;
        if (ring.empty() || distance(ring.back(), {p.x, p.y}) >= kSamePoint) ring.push_back({p.x, p.y});
    }
    if (ring.size() >= 2 && distance(ring.front(), ring.back()) < kSamePoint) ring.pop_back();
    const std::size_t n = ring.size();
    if (n < 3) return result;

    std::vector<std::vector<Vec2>> bands;
    for (const Wall& wall : floor.walls) {
        for (std::vector<Vec2>& band : wallOutline(floor, wall)) {
            if (band.size() >= 3) bands.push_back(std::move(band));
        }
    }

    std::vector<std::vector<Vec2>> lines;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = ring[i];
        const Vec2 b = ring[(i + 1) % n];
        const double length = distance(a, b);
        if (length < kSamePoint) continue;
        std::vector<double> cuts{0.0, 1.0};
        for (const std::vector<Vec2>& band : bands) {
            for (std::size_t k = 0; k < band.size(); ++k) {
                const Vec2 p = band[k];
                if (const auto t = crossingAt(a, b, p, band[(k + 1) % band.size()])) cuts.push_back(*t);
                const SegmentProjection closest = projectOntoSegment(p, a, b);
                if (closest.distance <= kWallTouch + 1e-9) cuts.push_back(closest.offset / length);
            }
        }
        std::sort(cuts.begin(), cuts.end());
        for (std::size_t k = 0; k + 1 < cuts.size(); ++k) {
            const double t0 = cuts[k];
            const double t1 = cuts[k + 1];
            if ((t1 - t0) * length < kSamePoint) continue;
            const double mid = (t0 + t1) / 2.0;
            if (onWalls({a.x + (b.x - a.x) * mid, a.y + (b.y - a.y) * mid}, bands)) continue;
            const Vec2 from{a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0};
            const Vec2 to{a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1};
            if (!lines.empty() && distance(lines.back().back(), from) < kSamePoint) {
                lines.back().push_back(to);
            } else {
                lines.push_back({from, to});
            }
        }
    }
    // A stretch that runs on past the outline's first point joins the stretch that starts there.
    if (lines.size() > 1 && distance(lines.back().back(), lines.front().front()) < kSamePoint) {
        std::vector<Vec2> joined = lines.back();
        joined.insert(joined.end(), lines.front().begin() + 1, lines.front().end());
        lines.front() = std::move(joined);
        lines.pop_back();
    }
    for (const std::vector<Vec2>& line : lines) {
        std::vector<Vec2> tidy = withoutStraightPoints(line);
        const bool loop = tidy.size() >= 4 && distance(tidy.front(), tidy.back()) < kSamePoint;
        // A whole loop may start on a straight stretch; start it at a corner instead.
        if (loop && std::fabs(cross(tidy[tidy.size() - 2], tidy.front(), tidy[1])) < 1e-12) {
            tidy.pop_back();
            tidy.erase(tidy.begin());
            tidy.push_back(tidy.front());
        }
        std::vector<PlanXY> out;
        for (const Vec2& p : tidy) out.push_back({p.x, p.y});
        if (out.size() >= 2) result.push_back(std::move(out));
    }
    return result;
}

}
