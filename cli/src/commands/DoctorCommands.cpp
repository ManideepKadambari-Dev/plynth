#include "../CatalogCommon.h"
#include "../Common.h"

#include <plynth/core/Blender.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/SetupCheck.h>

#include <memory>

namespace plynth::cli {

namespace {

struct DoctorOptions {
    bool noAddons = false;
    BlenderOption blender;
};

// Runs the setup check and prints its report. Always succeeds: scripts read "ready".
Answer doctor(const RunContext& context, const DoctorOptions& options) {
    const std::string command = "doctor";
    const SettingsLoad loaded = loadSettings();
    if (!loaded.status.ok()) {
        return failure(command, loaded.status);
    }
    const MachineSettings& settings = loaded.settings;
    SetupCheckOptions check;
    check.blender = blenderOptionPath(context, options.blender);
    check.probeAddons = !options.noAddons;
    check.settingsExist = loaded.exists;
    const SetupReport report = runSetupCheck(settings, check);
    return success(command, setupReportToJson(report));
}

}

std::vector<CommandSpec> doctorCommands() {
    CommandSpec spec;
    spec.words = {"doctor"};
    spec.description = "Check this machine: Blender, folders and add-ons. Installs and changes nothing.";
    spec.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<DoctorOptions>();
        sub.add_flag("--no-addons", options->noAddons, "Do not ask Blender about its add-ons.");
        addBlenderOption(sub, options->blender,
                         "Check only this blender.exe. It beats PLYNTH_BLENDER and the saved path.");
        return [options](const RunContext& context) { return doctor(context, *options); };
    };
    return {spec};
}

}
