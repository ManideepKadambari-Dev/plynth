#include "plynth/core/SettingsChecks.h"

#include "plynth/core/Library.h"
#include "plynth/core/ProjectFiles.h"

#include <string>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

// Refuses a folder that holds no library.
Status checkLibraryChoice(const fs::path& folder)
{
    if (isLibraryFolder(folder)) return {};
    return fail(ErrorCode::NoLibrary,
                pathToUtf8(folder) + " is not a Plynth library. Run plynth library init there, or pick another folder.");
}

// Refuses a projects folder that is not there.
Status checkProjectsChoice(const fs::path& folder)
{
    std::error_code ec;
    if (!folder.empty() && fs::is_directory(folder, ec)) return {};
    return fail(ErrorCode::NotFound, "The projects folder " + pathToUtf8(folder) + " does not exist.");
}

// Starts the picked Blender once to read its version, and refuses one that cannot build.
BlenderPickCheck checkBlenderChoice(const fs::path& file, std::stop_token stop)
{
    BlenderPickCheck check;
    std::error_code ec;
    if (file.empty() || !fs::is_regular_file(file, ec)) {
        check.status = fail(ErrorCode::BlenderNotFound, pathToUtf8(file) + " is not a file.");
        return check;
    }
    check.install = inspectBlender(file, stop);
    if (check.install.usable()) return check;
    if (stop.stop_requested()) {
        check.status = fail(ErrorCode::Cancelled, "The Blender check was cancelled.");
        return check;
    }
    std::string why;
    if (!check.install.status.ok()) {
        why = check.install.status.message;
    } else if (!check.install.version) {
        why = "its version could not be read.";
    } else {
        why = "it is older than Blender " + formatBlenderVersion(kMinBlenderVersion) + ".";
    }
    check.status = fail(ErrorCode::BlenderNotFound, pathToUtf8(file) + " cannot be used: " + why);
    return check;
}

// Refuses a triangle warning below 1.
Status checkTriangleWarning(long long triangles)
{
    if (triangles >= 1) return {};
    return fail(ErrorCode::InvalidValue, "The triangle warning must be at least 1.");
}

}
