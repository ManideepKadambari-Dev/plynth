#include "plynth/core/BuildParts.h"
#include "plynth/core/ExportGroups.h"

#include <algorithm>

namespace plynth {

namespace {

// The walls of every floor, every door and window fill and every build part whose maker names this group, in one
// group that also takes anything left over.
class StructureRule final : public ExportGroupRule {
public:
    std::string_view id() const override { return "structure"; }
    int order() const override { return 10; }

    // Walls per floor with walls, then doors and windows in plan order, then the kinds of the part makers.
    void addGroups(const Project& project, std::vector<ExportGroup>& out) const override
    {
        ExportGroup group;
        group.id = "structure";
        group.name = "Walls";
        group.rest = true;
        const TrimSettings& trim = project.settings.trim;
        const bool anyTrim = trim.skirting.enabled || trim.cornice.enabled || trim.casing.enabled || trim.sill.enabled;
        for (const Floor& floor : project.plan.floors) {
            if (!floor.walls.empty()) group.members.push_back({std::string(kKindWalls), floor.id});
        }
        for (const Floor& floor : project.plan.floors) {
            if (anyTrim && !floor.walls.empty() && !floor.rooms.empty()) group.members.push_back({std::string(kKindTrim), floor.id});
        }
        for (const Floor& floor : project.plan.floors) {
            for (const Opening& opening : floor.openings) {
                if (opening.type == OpeningType::Door) group.members.push_back({std::string(kKindDoor), opening.id});
                if (opening.type == OpeningType::Window) group.members.push_back({std::string(kKindWindow), opening.id});
            }
        }
        for (const PartMaker* maker : allPartMakers()) {
            if (maker->exportGroup() != "structure") continue;
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

const ExportGroupRuleRegistrar registrar(std::make_unique<StructureRule>());

}

}
