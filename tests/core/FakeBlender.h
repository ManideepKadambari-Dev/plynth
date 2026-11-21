#pragma once

#include "TestSupport.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace plynth::test {

// Copies the fake Blender program to folder/blender.exe and writes its version and mode files next to it.
// Returns the path of blender.exe. An empty mode leaves fake_mode.txt out, so the environment decides.
inline std::filesystem::path makeFakeBlender(const std::filesystem::path& folder, const std::string& version,
                                             const std::string& mode = "")
{
    std::filesystem::create_directories(folder);
    const std::filesystem::path exe = folder / "blender.exe";
    std::filesystem::copy_file(PLYNTH_FAKE_BLENDER, exe, std::filesystem::copy_options::overwrite_existing);
    std::ofstream(folder / "fake_version.txt") << version;
    if (!mode.empty()) std::ofstream(folder / "fake_mode.txt") << mode;
    return exe;
}

// A folder that holds plynth_worker.py, so runBlenderJob finds its scripts.
inline std::filesystem::path makeFakeScripts(const std::filesystem::path& folder)
{
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "plynth_worker.py") << "# stand in for the worker\n";
    return folder;
}

}
