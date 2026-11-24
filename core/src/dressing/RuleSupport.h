#pragma once

#include "plynth/core/Dressing.h"
#include "plynth/core/Items.h"
#include "plynth/core/SeededChoice.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace plynth::dressing {

inline constexpr double kPi = 3.14159265358979323846;

// The asset choice with an id, or null.
inline const AssetChoice* findAsset(const std::vector<AssetChoice>& assets, std::string_view id)
{
    for (const AssetChoice& a : assets) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

// Says whether a list of names holds one.
inline bool holds(const std::vector<std::string>& names, std::string_view name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

// The assets that carry a tag, in id order.
inline std::vector<const AssetChoice*> assetsWithTag(const std::vector<AssetChoice>& assets, std::string_view tag)
{
    std::vector<const AssetChoice*> out;
    for (const AssetChoice& a : assets) {
        if (holds(a.tags, tag)) out.push_back(&a);
    }
    std::sort(out.begin(), out.end(), [](const AssetChoice* x, const AssetChoice* y) { return x->id < y->id; });
    return out;
}

// The items standing in a room that can carry props: asset items, never generator items.
inline std::vector<const Item*> hostItems(const Floor& floor, const Room& room)
{
    std::vector<const Item*> out;
    for (const Item& item : floor.items) {
        if (item.room == room.id && !isGeneratorItem(item) && !item.asset.empty()) out.push_back(&item);
    }
    return out;
}

// The plan position of a point in an item's own axes (as in asset.blend: front at -y), by the mapping in Items.h.
inline Vec2 localToPlan(const Item& item, double lx, double ly)
{
    const double t = -item.rotation * kPi / 180.0;
    const double wx = lx * std::cos(t) - ly * std::sin(t);
    const double wy = lx * std::sin(t) + ly * std::cos(t);
    return {item.x + wx, item.y - wy};
}

// The footprint corners of a prop on the plan.
inline std::vector<Vec2> propFootprint(const DressingPlacement& p)
{
    Item box;
    box.x = p.position.x;
    box.y = p.position.y;
    box.rotation = p.rotation;
    box.size = p.size;
    const ItemFootprint fp = itemFootprint(box);
    return std::vector<Vec2>(fp.corners.begin(), fp.corners.end());
}

// The area two convex polygons share, by clipping one with the other.
inline double overlapArea(const std::vector<Vec2>& a, const std::vector<Vec2>& b)
{
    const double orientation = signedArea(b) >= 0.0 ? 1.0 : -1.0;
    std::vector<Vec2> clipped = a;
    for (std::size_t i = 0; i < b.size() && !clipped.empty(); ++i) {
        const Vec2 e0 = b[i];
        const Vec2 e1 = b[(i + 1) % b.size()];
        auto side = [&](Vec2 p) { return orientation * ((e1.x - e0.x) * (p.y - e0.y) - (e1.y - e0.y) * (p.x - e0.x)); };
        std::vector<Vec2> next;
        for (std::size_t j = 0; j < clipped.size(); ++j) {
            const Vec2 p = clipped[j];
            const Vec2 q = clipped[(j + 1) % clipped.size()];
            const double sp = side(p);
            const double sq = side(q);
            if (sp >= 0.0) next.push_back(p);
            if ((sp >= 0.0) != (sq >= 0.0)) {
                const double t = sp / (sp - sq);
                next.push_back({p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t});
            }
        }
        clipped = std::move(next);
    }
    return clipped.size() < 3 ? 0.0 : std::fabs(signedArea(clipped));
}

// Says whether a prop overlaps any of the ones listed.
inline bool overlapsAny(const DressingPlacement& p, const std::vector<DressingPlacement>& others)
{
    return std::any_of(others.begin(), others.end(), [&](const DressingPlacement& o) { return dressingOverlaps(p, o); });
}

// Picks one of the assets by seed and key. Null for an empty list.
inline const AssetChoice* pickAsset(std::uint32_t seed, const std::string& key, const std::vector<const AssetChoice*>& from)
{
    std::vector<std::string> ids;
    for (const AssetChoice* a : from) ids.push_back(a->id);
    const auto index = seededChoice(seed, key, ids);
    return index ? from[*index] : nullptr;
}

}
