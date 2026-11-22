#include "plynth/core/Exporter.h"

namespace plynth {

namespace {

// One FBX file with every object of the building.
class SingleExporter final : public Exporter {
public:
    std::string_view id() const override { return kSingleExporterId; }
    std::string_view name() const override { return "Single FBX"; }
    std::string_view description() const override { return "One FBX file with the whole building and its collision meshes."; }

    // One group that takes everything, named after the project.
    Status groups(const Project& project, std::vector<ExportGroup>& out) const override
    {
        ExportGroup group;
        group.id = std::string(kSingleExportGroup);
        group.file = exportFileStem(project.settings.name) + ".fbx";
        out.push_back(std::move(group));
        return {};
    }
};

const ExporterRegistrar registrar(std::make_unique<SingleExporter>());

}

}
