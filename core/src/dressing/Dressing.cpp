#include "plynth/core/Dressing.h"

#include "dressing/RuleSupport.h"
#include "plynth/core/Items.h"
#include "plynth/core/Rooms.h"

#include <algorithm>
#include <mutex>

namespace plynth {

namespace {

// The registry: every rule ever registered, kept in a function local static so start up order does not matter.
struct Registry {
    std::mutex mutex;
    std::vector<std::unique_ptr<DressingRule>> rules;
};

// The one registry.
Registry& registry()
{
    static Registry instance;
    return instance;
}

// Says whether text is a well formed rule id: lower case letters, digits and "_", starting with a letter.
bool wellFormedRuleId(std::string_view id)
{
    if (id.empty() || id.size() > 64 || id.front() < 'a' || id.front() > 'z') return false;
    for (char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

}

// Adds a rule unless its id is taken or malformed.
bool registerDressingRule(std::unique_ptr<DressingRule> rule)
{
    if (!rule || !wellFormedRuleId(rule->id())) return false;
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    for (const auto& existing : r.rules) {
        if (existing->id() == rule->id()) return false;
    }
    r.rules.push_back(std::move(rule));
    return true;
}

DressingRuleRegistrar::DressingRuleRegistrar(std::unique_ptr<DressingRule> rule)
{
    registerDressingRule(std::move(rule));
}

// Finds a rule by id.
const DressingRule* findDressingRule(std::string_view id)
{
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    for (const auto& rule : r.rules) {
        if (rule->id() == id) return rule.get();
    }
    return nullptr;
}

// Every rule, by order and then id.
std::vector<const DressingRule*> allDressingRules()
{
    Registry& r = registry();
    std::vector<const DressingRule*> out;
    {
        std::lock_guard lock(r.mutex);
        for (const auto& rule : r.rules) out.push_back(rule.get());
    }
    std::sort(out.begin(), out.end(), [](const DressingRule* a, const DressingRule* b) {
        if (a->order() != b->order()) return a->order() < b->order();
        return a->id() < b->id();
    });
    return out;
}

// Says whether two props overlap.
bool dressingOverlaps(const DressingPlacement& a, const DressingPlacement& b)
{
    const double low = std::max(a.z, b.z);
    const double high = std::min(a.z + a.size.height, b.z + b.size.height);
    if (high - low <= kItemTouchTolerance) return false;
    return dressing::overlapArea(dressing::propFootprint(a), dressing::propFootprint(b)) > kItemOverlapArea;
}

// Runs every rule on a room.
std::vector<DressingPlacement> dressRoom(const Floor& floor, const Room& room, const std::vector<AssetChoice>& assets,
                                         std::uint32_t seed)
{
    std::vector<DressingPlacement> placed;
    if (!roomShape(floor, room).closed) return placed;
    std::vector<AssetChoice> sorted = assets;
    std::sort(sorted.begin(), sorted.end(), [](const AssetChoice& a, const AssetChoice& b) { return a.id < b.id; });
    for (const DressingRule* rule : allDressingRules()) {
        if (placed.size() >= kMaxDressingPerRoom) break;
        std::vector<DressingPlacement> made;
        rule->place(DressingInput{floor, room, sorted, placed, seed}, made);
        for (DressingPlacement& p : made) {
            if (placed.size() >= kMaxDressingPerRoom) break;
            const bool clash = std::any_of(placed.begin(), placed.end(),
                                           [&](const DressingPlacement& other) { return dressingOverlaps(p, other); });
            if (clash) continue;
            p.room = room.id;
            if (p.rule.empty()) p.rule = std::string(rule->id());
            placed.push_back(std::move(p));
        }
    }
    return placed;
}

}
