#include "plynth/core/BuildParts.h"
#include "plynth/core/ExportGroups.h"

#include <algorithm>

namespace plynth {

namespace {

// Says whether a floor has a door or plain opening whose sill is on the floor, so the build makes a threshold.
bool hasThreshold(const Floor& floor)
{
    for (const Opening& opening : floor.openings) {
        if (opening.type != OpeningType::Window && opening.sill == 0.0) return true;
    }
    return false;
}

// Floors, ceilings and thresholds, plus every slab, stair, roof, light, railing and guard, and every build part
// whose maker names this group.
class SurfacesRule final : public ExportGroupRule {
public:
    std::string_view id() const override { return "surfaces"; }
    int order() const override { return 20; }

    // Floor and ceiling of each room, thresholds of each floor that has them, then whole kinds, the part makers'
    // kinds last.
    void addGroups(const Project& project, std::vector<ExportGroup>& out) const override
    {
        ExportGroup group;
        group.id = "surfaces";
        group.name = "Floors";
        for (const Floor& floor : project.plan.floors) {
            for (const Room& room : floor.rooms) {
                group.members.push_back({std::string(kKindFloor), room.id});
                group.members.push_back({std::string(kKindCeiling), room.id});
            }
        }
        for (const Floor& floor : project.plan.floors) {
            if (hasThreshold(floor)) group.members.push_back({std::string(kKindThresholds), floor.id});
        }
        for (const std::string_view kind : {kKindSlab, kKindStair, kKindRoof, kKindLight, kKindRailing, kKindGuard}) {
            group.members.push_back({std::string(kind), Id()});
        }
        for (const PartMaker* maker : allPartMakers()) {
            if (maker->exportGroup() != "surfaces") continue;
            for (const std::string& kind : maker->kinds()) {
                const ExportMember member{kind, Id()};
                if (std::find(group.members.begin(), group.members.end(), member) == group.members.end()) {
                    group.members.push_back(member);
                }
            }
        }
        out.push_back(std::move(group));
    }
};

const ExportGroupRuleRegistrar registrar(std::make_unique<SurfacesRule>());

}

}
