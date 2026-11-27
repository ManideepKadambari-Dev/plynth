#pragma once

#include "TestSupport.h"

#include "../../core/src/stores/StoreFetchFolder.h"

#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/StoreFetch.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>

namespace plynth::test {

// A short fetch folder for one test, in a folder of its own that also stands in for LOCALAPPDATA and the
// system drive, so no fetch of the test can reach the real ones. PLYNTH_FETCH_FOLDER names it while the
// object lives, for fetches that run inside an import. Everything is removed at the end.
class ScratchFetch {
public:
    // Makes the folder. Make it before an IsolatedUser, so it sits under the short temp folder of the run.
    explicit ScratchFetch(const std::string& tag)
        : root_(std::filesystem::temp_directory_path() / ("pz" + tag + std::to_string(GetCurrentProcessId()))),
          variable_("PLYNTH_FETCH_FOLDER", pathToUtf8(root_ / "f"))
    {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
        std::filesystem::create_directories(root_, ec);
    }

    // Removes the folder and everything in it.
    ~ScratchFetch()
    {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    ScratchFetch(const ScratchFetch&) = delete;
    ScratchFetch& operator=(const ScratchFetch&) = delete;

    // The fetch folder the choice picks from test values only, never from this Windows user.
    FetchFolderChoice choice() const
    {
        detail::FetchFolderInputs in;
        in.variable = root_ / "f";
        in.localAppData = root_ / "local";
        in.shortRoot = root_;
        in.userId = "5c7a9e01";
        in.shortPath = [](const std::filesystem::path& path) { return path; };
        return detail::chooseFetchFolder(in);
    }

    // Runs a fetch in that folder, after checking it is the test's own short folder.
    FetchRun run(const ImportFetch& fetch, const std::filesystem::path& blender, const BlenderRunOptions& base,
                 const std::filesystem::path& work, const std::function<void(const BlenderProgress&)>& onProgress = {},
                 std::stop_token stop = {}) const
    {
        const FetchFolderChoice picked = choice();
        std::filesystem::path own = (root_ / "f").lexically_normal();
        own.make_preferred();
        INFO(picked.status.message);
        REQUIRE(picked.status.ok());
        REQUIRE(picked.used == 1);
        REQUIRE(picked.folder == own);
        return detail::runImportFetchIn(picked, fetch, blender, base, work, onProgress, stop);
    }

private:
    std::filesystem::path root_;
    ScopedEnv variable_;
};

}
