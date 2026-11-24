#include "CatalogSupport.h"
#include "FakeBlender.h"
#include "MaterialCatalogSupport.h"

#include <plynth/core/Blender.h>
#include <plynth/core/BlenderJob.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/AssetImport.h>
#include <plynth/core/catalog/BlenderKit.h>
#include <plynth/core/catalog/MaterialImport.h>
#include <plynth/core/catalog/StoreFetch.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

constexpr const char* kItem = "0c1d4a2e-1111-2222-3333-444455556666";
const std::vector<std::string> kModes{"success", "failed", "crash", "bad_result", "child", "forged", "hang"};
const std::vector<std::string> kSteps{"check_addon", "download", "copy"};

// Says whether a text holds one of the made-up secrets.
bool leaks(const std::string& text)
{
    return text.find("KEY-MARKER-7f31") != std::string::npos || text.find("TOKEN-MARKER-92ac") != std::string::npos;
}

// The files under a folder that hold a made-up secret.
std::vector<std::string> leakingFiles(const fs::path& root)
{
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        std::ifstream in(it->path(), std::ios::binary);
        if (leaks({std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()})) {
            out.push_back(pathToUtf8(it->path().lexically_relative(root)));
        }
    }
    return out;
}

// The progress lines a run handed on, each step once and in the order it first came.
struct Heard {
    std::vector<std::string> steps;
    std::vector<std::string> texts;
    std::mutex mutex;

    // Keeps one progress line.
    void add(const BlenderProgress& p)
    {
        std::lock_guard lock(mutex);
        texts.push_back(p.step);
        texts.push_back(p.message);
        if (!p.step.empty() && std::find(steps.begin(), steps.end(), p.step) == steps.end()) steps.push_back(p.step);
    }

    // Says whether any line held a secret.
    bool leaked() const { return std::any_of(texts.begin(), texts.end(), leaks); }
};

// The leaking stand-in Blender with the fake Blender inside it.
fs::path leakyBlender(const fs::path& folder, const std::string& mode)
{
    const fs::path exe = plynth::test::makeFakeBlender(folder, "5.2.0");
    fs::rename(exe, folder / "fake_inner.exe");
    fs::copy_file(PLYNTH_LEAKY_BLENDER, exe);
    std::ofstream(folder / "leaky_mode.txt") << mode;
    return exe;
}

// An import context on the leaking Blender that never uses the Recycle Bin.
ImportContext contextFor(const LibraryPaths& paths, const fs::path& root, const std::string& mode)
{
    ImportContext context;
    context.library = paths;
    context.blender = inspectBlender(leakyBlender(root / "b", mode));
    context.run.scriptsFolder = plynth::test::makeFakeScripts(root / "s");
    context.removeOriginal = [](const fs::path& path) {
        std::error_code ec;
        fs::remove(path, ec);
        return Status{};
    };
    return context;
}

// Stops a run that would hang.
std::jthread stopSoon(std::stop_source& source, const std::string& mode)
{
    return std::jthread([&source, hangs = mode == "hang"](std::stop_token done) {
        if (!hangs) return;
        for (int i = 0; i < 30 && !done.stop_requested(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        source.request_stop();
    });
}

// What a mode ends with.
ErrorCode expectedCode(const std::string& mode)
{
    if (mode == "hang") return ErrorCode::Cancelled;
    if (mode == "success" || mode == "child" || mode == "forged") return ErrorCode::None;
    return ErrorCode::BlenderFailed;
}

}

TEST_CASE("a store fetch stores and returns none of what Blender prints", "[core][fetch][store_key]")
{
    for (const std::string& mode : kModes) {
        for (const bool timeout : {false, true}) {
            if (timeout && mode != "hang") continue;
            INFO(mode << (timeout ? " until the timeout" : ""));
            plynth::test::IsolatedUser user("kf");
            plynth::test::TempDir tmp("kf");
            BlenderRunOptions base;
            base.scriptsFolder = plynth::test::makeFakeScripts(tmp.path() / "s");
            ImportFetch fetch = makeBlenderKitFetch(kItem, kFetchKindModel, BlenderKitOptions{});
            if (timeout) fetch.timeout = std::chrono::seconds(2);

            Heard heard;
            std::stop_source stop;
            FetchRun run;
            {
                const std::jthread guard = stopSoon(stop, timeout ? "" : mode);
                run = runImportFetch(fetch, leakyBlender(tmp.path() / "b", mode), base, tmp.path() / "w",
                                     [&](const BlenderProgress& p) { heard.add(p); }, stop.get_token());
            }
            CHECK(run.status.code == (timeout ? ErrorCode::BlenderFailed : expectedCode(mode)));
            CHECK(leakingFiles(tmp.path() / "w") == std::vector<std::string>{});
            CHECK_FALSE(fs::exists(tmp.path() / "w" / "result.json"));
            CHECK_FALSE(leaks(run.status.message));
            CHECK_FALSE(leaks(run.status.where));
            CHECK_FALSE(leaks(run.errorCode));
            CHECK_FALSE(leaks(run.output.header.status.message));
            CHECK_FALSE(heard.leaked());
            if (mode == "success" || mode == "child" || mode == "forged") CHECK(heard.steps == kSteps);
            if (mode == "crash") CHECK(heard.steps == std::vector<std::string>{"check_addon", "download"});
        }
    }
}

TEST_CASE("an asset import from a store keeps and shows none of what the fetch prints", "[store_import][store_key]")
{
    for (const std::string& mode : kModes) {
        INFO(mode);
        plynth::test::IsolatedUser user("ka");
        plynth::test::TestLibrary lib("ka");
        const ImportContext context = contextFor(lib.paths, lib.tmp.path(), mode);
        ImportRequest request;
        request.source.url = std::string("asset_base_id:") + kItem;

        Heard heard;
        std::stop_source stop;
        ImportOutcome outcome;
        {
            const std::jthread guard = stopSoon(stop, mode);
            outcome = importAsset(*lib.catalog, context, request,
                                  [&](const ImportProgress& p) { heard.add(p.blender); }, stop.get_token());
        }
        CHECK(outcome.status.code == expectedCode(mode));
        CHECK(leakingFiles(lib.paths.root) == std::vector<std::string>{});
        CHECK_FALSE(leaks(outcome.status.message));
        for (const BlenderWarning& warning : outcome.warnings) CHECK_FALSE(leaks(warning.message));
        CHECK_FALSE(heard.leaked());
    }
}

TEST_CASE("a material import from a store keeps and shows none of what the fetch prints", "[store_import][store_key]")
{
    for (const std::string& mode : kModes) {
        INFO(mode);
        plynth::test::IsolatedUser user("km");
        plynth::test::MaterialTestLibrary lib("km");
        const ImportContext context = contextFor(lib.paths, lib.tmp.path(), mode);
        MaterialImportRequest request;
        request.source.url = std::string("asset_base_id:") + kItem;

        Heard heard;
        std::stop_source stop;
        MaterialImportOutcome outcome;
        {
            const std::jthread guard = stopSoon(stop, mode);
            outcome = importMaterial(*lib.catalog, context, request,
                                     [&](const ImportProgress& p) { heard.add(p.blender); }, stop.get_token());
        }
        CHECK(outcome.status.code == expectedCode(mode));
        CHECK(leakingFiles(lib.paths.root) == std::vector<std::string>{});
        CHECK_FALSE(leaks(outcome.status.message));
        for (const BlenderWarning& warning : outcome.warnings) CHECK_FALSE(leaks(warning.message));
        CHECK_FALSE(heard.leaked());
    }
}

TEST_CASE("a forged progress line keeps only the sentence that belongs to its step", "[core][fetch][store_key]")
{
    plynth::test::IsolatedUser user("kp");
    plynth::test::TempDir tmp("kp");
    BlenderRunOptions base;
    base.scriptsFolder = plynth::test::makeFakeScripts(tmp.path() / "s");
    std::vector<BlenderProgress> lines;
    const FetchRun run = runImportFetch(makeBlenderKitFetch(kItem, kFetchKindModel, BlenderKitOptions{}),
                                        leakyBlender(tmp.path() / "b", "forged"), base, tmp.path() / "w",
                                        [&](const BlenderProgress& p) { lines.push_back(p); });
    plynth::test::requireOk(run.status);
    REQUIRE_FALSE(lines.empty());
    for (const BlenderProgress& line : lines) {
        INFO(line.step << ": " << line.message);
        if (line.step == "check_addon") CHECK(line.message == "Checking the BlenderKit add-on");
        else if (line.step == "download") CHECK(line.message == "Downloading from BlenderKit");
        else if (line.step == "copy") CHECK(line.message == "Copying the download");
        else FAIL_CHECK("a step the fetch does not have was passed on");
    }
}

TEST_CASE("a run without factory startup returns only rebuilt lines of the steps it was given", "[core][blenderjob][store_key]")
{
    for (const std::string& mode : {std::string("forged"), std::string("crash"), std::string("bad_result")}) {
        for (const bool listed : {true, false}) {
            INFO(mode << (listed ? " with steps" : " without steps"));
            plynth::test::IsolatedUser user("kj");
            plynth::test::TempDir tmp("kj");
            const fs::path work = tmp.path() / "w";
            BlenderRunOptions options;
            options.factoryStartup = false;
            options.scriptsFolder = plynth::test::makeFakeScripts(tmp.path() / "s");
            if (listed) options.steps = {{"check_addon", "Sentence one"}, {"download", "Sentence two"}};
            Json params = makeBlenderKitFetch(kItem, kFetchKindModel, BlenderKitOptions{}).params;
            params["output_folder"] = pathToUtf8(work / "fetched");
            const Json job = makeBlenderJob(kBlenderKitFetchTask, "leak-1", work / "result.json", params);

            std::vector<BlenderProgress> lines;
            const BlenderJobRun run = runBlenderJob(leakyBlender(tmp.path() / "b", mode), job, work, options,
                                                    [&](const BlenderProgress& p) { lines.push_back(p); });
            CHECK_FALSE(leaks(run.logTail));
            CHECK_FALSE(leaks(run.status.message));
            CHECK_FALSE(leaks(run.result.dump()));
            CHECK(run.logTail.find("not json") == std::string::npos);
            CHECK(leakingFiles(work) == std::vector<std::string>{});
            CHECK_FALSE(fs::exists(work / "result.json"));
            if (!listed) CHECK(lines.empty());
            if (listed) CHECK_FALSE(lines.empty());
            for (std::size_t i = 0; i < lines.size(); ++i) {
                INFO(lines[i].step << ": " << lines[i].message);
                const bool first = lines[i].step == "check_addon";
                CHECK((first || lines[i].step == "download"));
                CHECK(lines[i].index == (first ? 1 : 2));
                CHECK(lines[i].count == 2);
                CHECK(lines[i].message == (first ? "Sentence one" : "Sentence two"));
                if (i > 0) CHECK(lines[i].step != lines[i - 1].step);
            }
        }
    }
}
