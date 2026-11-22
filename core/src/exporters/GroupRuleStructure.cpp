#include "plynth/core/ExportGroups.h"

namespace plynth {

namespace {

// The walls of every floor and every door and window fill, in one group that also takes anything left over.
class StructureRule final : public ExportGroupRule {
public:
    std::string_view id() const override { return "structure"; }
    int order() const override { return 10; }

    // Walls per floor with walls, then doors and windows in plan order.
    void addGroups(const Project& project, std::vector<ExportGroup>& out) const override
    {
        ExportGroup group;
        group.id = "structure";
        group.name = "Walls";
        group.rest = true;
        for (const Floor& floor : project.plan.floors) {
            if (!floor.walls.empty()) group.members.push_back({std::string(kKindWalls), floor.id});
        }
        for (const Floor& floor : project.plan.floors) {
            for (const Opening& opening : floor.openings) {
                if (opening.type == OpeningType::Door) group.members.push_back({std::string(kKindDoor), opening.id});
                if (opening.type == OpeningType::Window) group.members.push_back({std::string(kKindWindow), opening.id});
            }
        }
        out.push_back(std::move(group));
    }
};

const ExportGroupRuleRegistrar registrar(std::make_unique<StructureRule>());

}

}
