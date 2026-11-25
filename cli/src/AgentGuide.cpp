#include "AgentGuide.h"

#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Resources.h>

#include <fstream>
#include <iterator>
#include <string>

namespace plynth::cli {

namespace fs = std::filesystem;

AgentGuideWrite writeAgentGuide(const fs::path& projectFolder, bool overwrite) {
    AgentGuideWrite out;
    std::error_code ec;
    const fs::path target = projectFolder / kAgentGuideFileName;
    if (fs::exists(target, ec) && !overwrite) {
        return out;
    }
    const fs::path docs = resourceFolder(ResourceKind::Docs);
    const fs::path master = docs / "templates" / "project-agents.md";
    std::ifstream in(master, std::ios::binary);
    if (docs.empty() || !in) {
        out.status = fail(ErrorCode::NotFound, "The instructions file for projects was not found next to the program.");
        return out;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::ofstream file(target, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    if (!file) {
        out.status = fail(ErrorCode::CannotWrite, "The file " + pathToUtf8(target) + " could not be written.");
        return out;
    }
    out.result = AgentGuideResult::Written;
    return out;
}

}
