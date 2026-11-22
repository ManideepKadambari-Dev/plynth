#include "plynth/core/ExportGroups.h"
#include "plynth/core/MaterialSlots.h"

#include <algorithm>
#include <set>

namespace plynth {

namespace {

// A group id part: lower case, anything other than a to z, 0 to 9 and "_" turned into "_".
std::string idPart(const std::string& roomId)
{
    std::string out;
    for (const char c : roomId) {
        const char lower = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        const bool ok = (lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9') || lower == '_';
        out.push_back(ok ? lower : '_');
    }
    return out;
}

// A room and the items that stand in it.
struct RoomItems {
    const Room* room = nullptr;
    std::vector<const Item*> items;
};

// One group of furniture for each room that holds items, and one for items with no room.
class FurnitureRule final : public ExportGroupRule {
public:
    std::string_view id() const override { return "furniture"; }
    int order() const override { return 30; }

    // Items and their generators per room in plan order, then the items that stand in no room.
    void addGroups(const Project& project, std::vector<ExportGroup>& out) const override
    {
        std::vector<RoomItems> rooms;
        std::vector<const Item*> loose;
        for (const Floor& floor : project.plan.floors) {
            const std::size_t firstOfFloor = rooms.size();
            for (const Room& room : floor.rooms) rooms.push_back({&room, {}});
            for (const Item& item : floor.items) {
                RoomItems* home = nullptr;
                if (item.room) {
                    for (std::size_t i = firstOfFloor; i < rooms.size(); ++i) {
                        if (rooms[i].room->id == *item.room) home = &rooms[i];
                    }
                }
                if (home) home->items.push_back(&item);
                else loose.push_back(&item);
            }
        }
        rooms.erase(std::remove_if(rooms.begin(), rooms.end(), [](const RoomItems& r) { return r.items.empty(); }), rooms.end());

        const auto labelCount = [&](const std::string& label) {
            return std::count_if(rooms.begin(), rooms.end(), [&](const RoomItems& r) { return roomSlotLabel(*r.room) == label; });
        };
        std::set<std::string> usedIds;
        for (const RoomItems& entry : rooms) {
            const std::string label = roomSlotLabel(*entry.room);
            ExportGroup group;
            std::string id = "furniture_" + idPart(entry.room->id);
            for (int n = 2; !usedIds.insert(id).second; ++n) id = "furniture_" + idPart(entry.room->id) + "_" + std::to_string(n);
            group.id = id;
            group.name = "Furniture " + label + (labelCount(label) > 1 ? " " + entry.room->id : std::string());
            addItems(group, entry.items);
            out.push_back(std::move(group));
        }
        if (!loose.empty()) {
            ExportGroup group;
            group.id = "furniture";
            group.name = "Furniture";
            addItems(group, loose);
            out.push_back(std::move(group));
        }
    }

private:
    // Adds the item and generator member of each item.
    static void addItems(ExportGroup& group, const std::vector<const Item*>& items)
    {
        for (const Item* item : items) {
            group.members.push_back({std::string(kKindItem), item->id});
            group.members.push_back({std::string(kKindGenerator), item->id});
        }
    }
};

const ExportGroupRuleRegistrar registrar(std::make_unique<FurnitureRule>());

}

}
