#pragma once

#include <filesystem>
#include <vector>

namespace plynth::discovery {

// Where Windows records an installed Blender: uninstall entries, App Paths and the .blend open command.
// Paths of blender.exe that may or may not exist; the caller checks.
std::vector<std::filesystem::path> registryBlenders();

// Microsoft Store packages named BlenderFoundation.Blender* and the blender*.exe aliases in WindowsApps.
std::vector<std::filesystem::path> storeBlenders();

// steamapps/common/Blender/blender.exe in every Steam library.
std::vector<std::filesystem::path> steamBlenders();

// The copies Plynth downloaded: root/*/blender.exe in each folder that holds the download marker and is not a
// link. Sorted by path.
std::vector<std::filesystem::path> downloadedBlenders(const std::filesystem::path& root);

}
