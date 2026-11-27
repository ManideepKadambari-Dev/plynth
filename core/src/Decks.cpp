#include "plynth/core/Decks.h"

#include "plynth/core/Geometry.h"

#include <algorithm>
#include <cmath>

namespace plynth {

namespace {

// Says whether a number is finite and from low to high, allowing for rounding.
bool inRange(double v, double low, double high)
{
    return std::isfinite(v) && v >= low - 1e-9 && v <= high + 1e-9;
}

// Says whether text is a catalog name: letters, digits, "_" and "-", at most 128 characters.
bool wellFormedName(const std::string& name)
{
    if (name.empty() || name.size() > 128) return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

// The area a closed outline encloses, in square metres.
double outlineArea(const std::vector<PlanXY>& outline)
{
    double twice = 0.0;
    for (std::size_t i = 0; i < outline.size(); ++i) {
        const PlanXY& a = outline[i];
        const PlanXY& b = outline[(i + 1) % outline.size()];
        twice += a.x * b.y - b.x * a.y;
    }
    return std::fabs(twice) / 2.0;
}

}

// Checks the values a deck command would set.
Status checkDeck(const Deck& deck)
{
    for (const PlanXY& p : deck.outline) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
            return fail(ErrorCode::InvalidValue, "The deck outline points must be finite numbers.");
        }
    }
    if (deck.outline.size() < 3) return fail(ErrorCode::InvalidValue, "A deck outline needs at least three points.");
    if (!(outlineArea(deck.outline) > 0.01)) {
        return fail(ErrorCode::InvalidValue, "The deck outline must enclose more than 0.01 square metres.");
    }
    if (!inRange(deck.top, 0.0, 10.0)) return fail(ErrorCode::InvalidValue, "The deck top must be from 0 to 10 metres.");
    if (!inRange(deck.thickness, 0.05, 1.0)) {
        return fail(ErrorCode::InvalidValue, "The deck thickness must be from 0.05 to 1 metre.");
    }
    if (deck.material && !wellFormedName(*deck.material)) {
        return fail(ErrorCode::InvalidValue, "The deck material \"" + *deck.material + "\" is not a valid name.");
    }
    if (deck.skirtMaterial && !wellFormedName(*deck.skirtMaterial)) {
        return fail(ErrorCode::InvalidValue, "The skirt material \"" + *deck.skirtMaterial + "\" is not a valid name.");
    }
    return {};
}

// Finds the highest deck of the floor under a plan point and gives its top.
std::optional<double> deckTopAt(const Floor& floor, double x, double y)
{
    std::optional<double> top;
    for (const Deck& deck : floor.decks) {
        if (deck.outline.size() < 3) continue;
        std::vector<Vec2> outline;
        for (const PlanXY& p : deck.outline) outline.push_back({p.x, p.y});
        if (!insidePolygon({x, y}, outline)) continue;
        if (!top || deck.top > *top) top = deck.top;
    }
    return top;
}

}
