#include "plynth/core/AgentFiles.h"

#include "FileIo.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/Resources.h"
#include "plynth/core/Samples.h"

#include <fstream>
#include <iterator>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// The path as an absolute path, or as it is when that fails.
fs::path absoluteOf(const fs::path& path)
{
    std::error_code ec;
    fs::path out = fs::absolute(path, ec);
    return ec ? path : out.lexically_normal();
}

// The template for the skill file in the docs folder found next to the program, or empty.
fs::path skillTemplate()
{
    const fs::path docs = resourceFolder(ResourceKind::Docs);
    if (docs.empty()) return {};
    std::error_code ec;
    const fs::path file = docs / pathFromUtf8(kSkillTemplate);
    return fs::is_regular_file(file, ec) ? file : fs::path{};
}

// Reads a whole file into text. False when it cannot be opened.
bool readText(const fs::path& file, std::string& text)
{
    if (file.empty()) return false;
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

// Copies a template to target unless target is there and overwrite is false.
AgentFileWrite copyTemplate(const fs::path& source, const fs::path& target, bool overwrite,
                            const std::string& missingMessage)
{
    AgentFileWrite out;
    out.file = absoluteOf(target);
    std::error_code ec;
    if (fs::exists(target, ec) && !overwrite) return out;
    std::string text;
    if (!readText(source, text)) {
        out.status = fail(ErrorCode::NotFound, missingMessage);
        return out;
    }
    if (!fileio::writeAtomic(target, text).ok()) {
        out.status = fail(ErrorCode::CannotWrite, "The file " + pathToUtf8(out.file) + " could not be written.");
        return out;
    }
    out.result = AgentFileResult::Written;
    return out;
}

// A line without the spaces, tabs and carriage return at its end.
std::string_view trimEnd(std::string_view line)
{
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
    return line;
}

}

// Copies the AGENTS.md template into a project folder, keeping one that is there unless overwrite is true.
AgentFileWrite writeAgentGuide(const fs::path& projectFolder, bool overwrite)
{
    if (projectFolder.empty()) {
        AgentFileWrite out;
        out.status = fail(ErrorCode::CannotWrite, "No project folder was given for AGENTS.md.");
        return out;
    }
    return copyTemplate(agentGuideMaster(), projectFolder / pathFromUtf8(kAgentGuideFileName), overwrite,
                        "The instructions file for projects was not found next to the program.");
}

// Writes the skill file into its own folder inside the skills folder, keeping one that is there unless force is true.
AgentFileWrite installSkill(const fs::path& skillsFolder, bool force)
{
    const fs::path folder = skillsFolder / pathFromUtf8(kSkillFolderName);
    const fs::path target = folder / pathFromUtf8(kSkillFileName);
    std::error_code ec;
    if (skillsFolder.empty() || !fs::is_directory(skillsFolder, ec)) {
        AgentFileWrite out;
        out.file = absoluteOf(target);
        out.status =
            fail(ErrorCode::NotFound, "The folder " + pathToUtf8(absoluteOf(skillsFolder)) + " does not exist.");
        return out;
    }
    const fs::path source = skillTemplate();
    const std::string missing = "The skill file was not found next to the program.";
    if (force || !fs::exists(target, ec)) {
        AgentFileWrite out;
        out.file = absoluteOf(target);
        if (source.empty()) {
            out.status = fail(ErrorCode::NotFound, missing);
            return out;
        }
        if (!fs::exists(folder, ec)) fs::create_directory(folder, ec);
        if (!fs::is_directory(folder, ec)) {
            out.status = fail(ErrorCode::CannotWrite,
                              "The folder " + pathToUtf8(absoluteOf(folder)) + " could not be made.");
            return out;
        }
    }
    return copyTemplate(source, target, force, missing);
}

// The lines between the shared begin and end lines, each ending with a plain line break.
std::string sharedAgentText(std::string_view templateText)
{
    std::string shared;
    bool inside = false;
    std::size_t start = 0;
    while (start < templateText.size()) {
        std::size_t end = templateText.find('\n', start);
        if (end == std::string_view::npos) end = templateText.size();
        const std::string_view line = trimEnd(templateText.substr(start, end - start));
        start = end + 1;
        if (line == kSharedEndLine) return inside ? shared : std::string{};
        if (line == kSharedBeginLine) {
            if (inside) return {};
            inside = true;
            continue;
        }
        if (inside) {
            shared.append(line);
            shared.push_back('\n');
        }
    }
    return {};
}

}
