#pragma once

#include "Blender.h"
#include "Status.h"

#include <filesystem>
#include <stop_token>

namespace plynth {

// The checks a machine setting must pass before it is saved (issue 42). "plynth settings set" and the app's setup
// check pickers (library, projects folder, Blender) and settings dialog call these, so both refuse the same
// values with the same sentences. Each takes an absolute path.

// NoLibrary "<folder> is not a Plynth library. Run plynth library init there, or pick another folder." when
// isLibraryFolder is false. The app then offers to make one there (initLibrary) and checks again.
Status checkLibraryChoice(const std::filesystem::path& folder);

// NotFound "The projects folder <folder> does not exist." when it is not a folder.
Status checkProjectsChoice(const std::filesystem::path& folder);

// What a Blender pick gives.
struct BlenderPickCheck {
    // BlenderNotFound "<file> is not a file." when it is missing, or "<file> cannot be used: <why>" when
    // inspectBlender failed or it is older than kMinBlenderVersion. Cancelled when stop was asked for.
    Status status;
    BlenderInstall install; // filled when it could be inspected
};

// Inspects the picked blender.exe (starts it once with --version). Blocks.
BlenderPickCheck checkBlenderChoice(const std::filesystem::path& file, std::stop_token stop = {});

// InvalidValue "The triangle warning must be at least 1." below 1.
Status checkTriangleWarning(long long triangles);

}
