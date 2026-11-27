#include "plynth/core/NewProject.h"

#include "plynth/core/ProjectFiles.h"

#include <array>
#include <string>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// The name with its ASCII letters in upper case.
std::string upperAscii(std::string_view text)
{
    std::string out(text);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

// True for a name Windows keeps for a device, such as CON or COM1, with or without an ending.
bool isDeviceName(std::string_view name)
{
    std::string_view base = name.substr(0, name.find('.'));
    // Windows ignores spaces at the end of the part before the dot, so "nul .txt" is the device too.
    while (!base.empty() && base.back() == ' ') base.remove_suffix(1);
    const std::string upper = upperAscii(base);
    static constexpr std::array<std::string_view, 4> plain{"CON", "PRN", "AUX", "NUL"};
    for (std::string_view device : plain) {
        if (upper == device) return true;
    }
    return upper.size() == 4 && (upper.starts_with("COM") || upper.starts_with("LPT")) && upper[3] >= '1' &&
           upper[3] <= '9';
}

// A path written with the separators Windows shows.
std::string shown(const fs::path& path)
{
    fs::path copy = path;
    return pathToUtf8(copy.make_preferred());
}

}

// Checks a project name as a Windows folder name, with the sentences of the New project dialog.
Status checkProjectName(std::string_view name)
{
    if (name.find_first_not_of(' ') == std::string_view::npos) {
        return fail(ErrorCode::InvalidValue, "Type a name for the project.");
    }
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos) {
            return fail(ErrorCode::InvalidValue,
                        "A folder name cannot contain any of these characters: \\ / : * ? \" < > |");
        }
    }
    if (name.back() == '.') return fail(ErrorCode::InvalidValue, "A folder name cannot end with a dot.");
    if (name.back() == ' ') return fail(ErrorCode::InvalidValue, "A folder name cannot end with a space.");
    if (isDeviceName(name)) {
        return fail(ErrorCode::InvalidValue,
                    "Windows keeps the name " + std::string(name) + " for itself. Choose another name.");
    }
    return {};
}

// Checks the name, then the location, then the folder they make together.
Status checkNewProjectFolder(const fs::path& location, std::string_view name)
{
    if (Status s = checkProjectName(name); !s.ok()) return s;
    if (location.empty()) return fail(ErrorCode::InvalidValue, "Choose a location for the project.");
    std::error_code ec;
    if (fs::exists(location, ec) && !fs::is_directory(location, ec)) {
        return fail(ErrorCode::InvalidValue, "The location " + shown(location) + " is a file, not a folder.");
    }
    if (!location.is_absolute()) {
        return fail(ErrorCode::InvalidValue, "Choose a full location, such as one picked with Browse.");
    }
    const fs::path target = location / pathFromUtf8(name);
    const std::string nameText(name);
    const fs::file_status status = fs::status(target, ec);
    if (!fs::exists(status)) return {};
    if (!fs::is_directory(status)) {
        return fail(ErrorCode::AlreadyExists,
                    "The location already has a file named " + nameText + ". Choose another name.");
    }
    if (isProjectFolder(target)) {
        return fail(ErrorCode::AlreadyExists, "The folder " + shown(target) + " already holds a project.");
    }
    fs::directory_iterator entries(target, ec);
    if (ec) return fail(ErrorCode::CannotRead, "The folder " + shown(target) + " could not be read.");
    if (entries != fs::directory_iterator()) {
        return fail(ErrorCode::AlreadyExists, "The location already has a folder named " + nameText +
                                                  " with files in it. Choose another name or location.");
    }
    return {};
}

// Splits a whole folder path into its parent and its last part, then checks them.
Status checkNewProjectFolder(const fs::path& folder)
{
    if (folder.empty()) return fail(ErrorCode::InvalidValue, "Choose a folder for the project.");
    fs::path normal = folder.lexically_normal();
    if (!normal.has_filename() && normal.has_relative_path()) normal = normal.parent_path();
    if (!normal.has_filename()) {
        return fail(ErrorCode::InvalidValue, "A project cannot be made in the top folder of a drive.");
    }
    return checkNewProjectFolder(normal.parent_path(), pathToUtf8(normal.filename()));
}

// Checks the folder, saves the new project there and adds AGENTS.md when asked.
NewProjectResult createNewProject(Project& project, const fs::path& folder, const fs::path& sourceFolder,
                                  const NewProjectOptions& options)
{
    NewProjectResult out;
    out.status = checkNewProjectFolder(folder);
    if (!out.status.ok()) return out;
    std::error_code ec;
    const bool existed = fs::exists(folder, ec);
    out.status = createProject(project, folder, sourceFolder);
    if (!out.status.ok()) {
        // A folder this call made would block the next try, since a folder with files in it is refused.
        if (!existed) fs::remove_all(folder, ec);
        return out;
    }
    if (options.agentGuide) out.agentGuide = writeAgentGuide(folder, false);
    return out;
}

}
