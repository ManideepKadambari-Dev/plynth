#pragma once

#include "TestSupport.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <cstdlib>

namespace plynth::cli::testing {

// Copies the fake Blender into folder as blender.exe, with a version file next to it. Returns its path.
inline std::filesystem::path installFakeBlender(const std::filesystem::path& folder, const std::string& version,
                                                const std::string& mode = "") {
    std::filesystem::create_directories(folder);
    const std::filesystem::path exe = folder / "blender.exe";
    std::filesystem::copy_file(PLYNTH_FAKE_BLENDER, exe, std::filesystem::copy_options::overwrite_existing);
    std::ofstream(folder / "fake_version.txt") << version;
    if (!mode.empty()) {
        std::ofstream(folder / "fake_mode.txt") << mode;
    }
    return exe;
}

// Sets an environment variable for the life of this object, then puts the old value back.
class ScopedEnvironment {
public:
    ScopedEnvironment(std::string name, const std::string& value) : name_(std::move(name)) {
        char* old = nullptr;
        std::size_t size = 0;
        if (_dupenv_s(&old, &size, name_.c_str()) == 0 && old != nullptr) {
            old_ = old;
            had_ = true;
        }
        std::free(old);
        _putenv_s(name_.c_str(), value.c_str());
    }
    ~ScopedEnvironment() { _putenv_s(name_.c_str(), had_ ? old_.c_str() : ""); }
    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

private:
    std::string name_;
    std::string old_;
    bool had_ = false;
};

}
