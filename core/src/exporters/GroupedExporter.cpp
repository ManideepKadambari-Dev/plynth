#include "plynth/core/ExportGroups.h"
#include "plynth/core/Exporter.h"

namespace plynth {

namespace {

// One FBX file for each automatic group: walls, floors and one for furniture per room.
class GroupedExporter final : public Exporter {
public:
    std::string_view id() const override { return kGroupedExporterId; }
    std::string_view name() const override { return "Grouped FBX"; }
    std::string_view description() const override
    {
        return "One FBX file each for walls, windows and doors, floors, and the furniture of each room, all at the building origin.";
    }
    bool reviewable() const override { return true; }

    // The automatic groups with their file names.
    Status groups(const Project& project, std::vector<ExportGroup>& out) const override
    {
        out = automaticExportGroups(project);
        nameExportGroupFiles(out, project.settings.name);
        return {};
    }
};

const ExporterRegistrar registrar(std::make_unique<GroupedExporter>());

}

}
