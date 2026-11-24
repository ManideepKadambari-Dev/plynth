#include "dressing/RuleSupport.h"

#include "plynth/core/Rooms.h"
#include "plynth/core/WallGeometry.h"

#include <optional>
#include <utility>

namespace plynth::dressing {

namespace {

constexpr double kFreeLength = 1.5;      // a wall face needs this much free length for a picture
constexpr double kPictureCentre = 1.5;   // the height of a picture's middle above the floor
constexpr double kTallFurniture = 1.2;   // furniture taller than this keeps pictures off the wall behind it
constexpr double kEndMargin = 0.3;       // kept free at each end of a wall, past the walls it meets
constexpr double kGap = 0.1;             // kept free beside openings, wall items and tall furniture
constexpr double kNearWall = 0.3;        // furniture this close to a wall face stands in front of it
constexpr std::size_t kMostPerRoom = 3;

// A stretch along a wall's centre line, from one offset to another.
using Span = std::pair<double, double>;

// The free stretches left of [from, to] once the blocked ones are taken out.
std::vector<Span> freeSpans(double from, double to, std::vector<Span> blocked)
{
    std::sort(blocked.begin(), blocked.end());
    std::vector<Span> out;
    double cursor = from;
    for (const Span& b : blocked) {
        if (b.second <= cursor) continue;
        if (b.first > cursor) out.push_back({cursor, std::min(b.first, to)});
        cursor = std::max(cursor, b.second);
        if (cursor >= to) break;
    }
    if (cursor < to) out.push_back({cursor, to});
    return out;
}

// One wall face that can hold a picture.
struct Candidate {
    const Wall* wall = nullptr;
    WallSide side = WallSide::Right;
    Span span;
};

// Pictures on the room's wall faces, at most one each and three in all, clear of openings and furniture.
class PicturesRule final : public DressingRule {
public:
    std::string_view id() const override { return "pictures"; }
    std::string_view name() const override { return "Pictures on walls"; }
    int order() const override { return 30; }

    void place(const DressingInput& in, std::vector<DressingPlacement>& out) const override
    {
        const std::vector<const AssetChoice*> pictures = assetsWithTag(in.assets, "wall_art");
        if (pictures.empty()) return;
        std::vector<Candidate> candidates;
        for (const Id& wallId : in.room.walls) {
            const Wall* wall = findWall(in.floor, wallId);
            if (!wall) continue;
            if (std::any_of(candidates.begin(), candidates.end(), [&](const Candidate& c) { return c.wall == wall; })) continue;
            const WallCurve curve = wallCurve(in.floor, *wall);
            if (!curve.valid || curve.length < kFreeLength + 2.0 * kEndMargin) continue;
            const std::optional<WallSide> side = roomSide(in, *wall, curve);
            if (!side) continue;
            std::vector<Span> blocked;
            for (const Opening& o : in.floor.openings) {
                if (o.wall == wall->id) blocked.push_back({o.offset - o.width / 2.0 - kGap, o.offset + o.width / 2.0 + kGap});
            }
            for (const Item& item : in.floor.items) {
                if (item.wall == wall->id) {
                    const double at = projectOntoCurve({item.x, item.y}, curve).offset;
                    blocked.push_back({at - item.size.width / 2.0 - kGap, at + item.size.width / 2.0 + kGap});
                    continue;
                }
                if (item.wall || item.z + item.size.height <= kTallFurniture) continue;
                const ItemFootprint fp = itemFootprint(item);
                double low = curve.length, high = 0.0;
                bool near = false;
                for (const Vec2& c : fp.corners) {
                    const CurveProjection p = projectOntoCurve(c, curve);
                    if (p.distance < wall->thickness / 2.0 + kNearWall) near = true;
                    low = std::min(low, p.offset);
                    high = std::max(high, p.offset);
                }
                if (near) blocked.push_back({low - kGap, high + kGap});
            }
            Span best{0.0, 0.0};
            for (const Span& s : freeSpans(kEndMargin, curve.length - kEndMargin, blocked)) {
                if (s.second - s.first > best.second - best.first + 1e-9) best = s;
            }
            if (best.second - best.first < kFreeLength - 1e-9) continue;
            candidates.push_back({wall, *side, best});
        }

        std::sort(candidates.begin(), candidates.end(), [&](const Candidate& a, const Candidate& b) {
            const auto sa = choiceScore(in.seed, in.room.id + "/pictures", a.wall->id);
            const auto sb = choiceScore(in.seed, in.room.id + "/pictures", b.wall->id);
            if (sa != sb) return sa > sb;
            return a.wall->id < b.wall->id;
        });
        std::size_t made = 0;
        for (const Candidate& c : candidates) {
            if (made >= kMostPerRoom) break;
            const double freeLength = c.span.second - c.span.first;
            const double wallTop = wallHeight(in.floor, *c.wall);
            std::vector<const AssetChoice*> fitting;
            for (const AssetChoice* a : pictures) {
                if (a->size.width <= freeLength - 2.0 * kGap + 1e-9 && a->size.height / 2.0 <= kPictureCentre
                    && kPictureCentre + a->size.height / 2.0 <= wallTop - 0.05) {
                    fitting.push_back(a);
                }
            }
            const AssetChoice* picture = pickAsset(in.seed, in.room.id + "/" + c.wall->id + "/picture", fitting);
            if (!picture) continue;
            const double offset = (c.span.first + c.span.second) / 2.0;
            const ItemPose pose = wallItemPose(in.floor, *c.wall, c.side, offset, picture->size);
            if (!pose.status.ok()) continue;
            DressingPlacement p;
            p.host = c.wall->id;
            p.rule = "pictures";
            p.asset = picture->id;
            p.position = pose.position;
            p.z = kPictureCentre - picture->size.height / 2.0;
            p.rotation = pose.rotation;
            p.size = picture->size;
            if (overlapsAny(p, in.placed) || overlapsAny(p, out)) continue;
            out.push_back(std::move(p));
            ++made;
        }
    }

private:
    // The side of a wall whose face looks into the room, empty when neither does.
    static std::optional<WallSide> roomSide(const DressingInput& in, const Wall& wall, const WallCurve& curve)
    {
        const Vec2 mid = pointAlong(curve, curve.length / 2.0);
        const Vec2 right = rightOf(tangentAlong(curve, curve.length / 2.0));
        const double reach = wall.thickness / 2.0 + 0.05;
        if (roomAt(in.floor, {mid.x + right.x * reach, mid.y + right.y * reach}) == in.room.id) return WallSide::Right;
        if (roomAt(in.floor, {mid.x - right.x * reach, mid.y - right.y * reach}) == in.room.id) return WallSide::Left;
        return std::nullopt;
    }
};

const DressingRuleRegistrar registrar(std::make_unique<PicturesRule>());

}

}
