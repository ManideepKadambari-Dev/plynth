#include "FakeBlender.h"
#include "SampleProject.h"
#include "SettingsGuard.h"

#include <plynth/core/LicenseReport.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;
using plynth::cli::testing::ScopedEnvironment;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;
namespace fs = std::filesystem;

namespace {

// Writes text to a file, making its folder.
void write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

// A docs folder with two pages.
fs::path makeDocs(const fs::path& root) {
    const fs::path docs = root / "docs";
    write(docs / "GETTING_STARTED.md", "# Getting started\n\nOpen the sample.\n\n## Stairs\n\nA stair joins floors.\n");
    write(docs / "USER_GUIDE.md", "# User guide\n\nStairs are drawn with the Stair tool.\n");
    return docs;
}

// A samples folder whose index names one project.
fs::path makeSamples(const fs::path& root) {
    const fs::path samples = root / "samples";
    write(samples / "samples.json", R"({"format": "plynth-samples", "version": 1, "license": "cc0", "assets": [], "materials": [],
 "projects": [{"id": "apartment", "name": "Apartment", "description": "One floor.", "folder": "projects/apartment",
               "image": "projects/apartment/plans/floor1.png"}]})");
    write(samples / "projects" / "apartment" / "project.json", "{}");
    write(samples / "projects" / "apartment" / "plan.json", "{}");
    write(samples / "projects" / "apartment" / "plans" / "floor1.png", "png");
    write(samples / "projects" / "apartment" / "build" / "building.blend", "output");
    return samples;
}

}

TEST_CASE("about gives the version and the three licenses", "[cli][guide]") {
    TempFolder temp("guide-about");
    write(temp.path() / "licenses" / "THIRD_PARTY_NOTICES.md", "# Notices\n");
    write(temp.path() / "licenses" / "texts" / "CC0-1.0.txt", "cc0");
    const ScopedEnvironment variable("PLYNTH_LICENSES", utf8(temp.path() / "licenses"));
    const auto outcome = runChecked({"about"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    CHECK(result.at("version") == PLYNTH_EXPECTED_VERSION);
    CHECK(result.at("source_license") == "GPL-3.0-or-later");
    CHECK(result.at("program_license") == "GPL-3.0-only");
    CHECK(result.at("sample_license") == "CC0-1.0");
    CHECK(result.at("copyright") == "Copyright 2026 the Plynth authors (see AUTHORS)");
    CHECK(result.at("license_files").size() == 1);
    CHECK_FALSE(result.at("notices_file").is_null());
}

TEST_CASE("docs list, show and search read the help pages", "[cli][guide]") {
    TempFolder temp("guide-docs");
    const fs::path docs = makeDocs(temp.path());
    const ScopedEnvironment variable("PLYNTH_DOCS", utf8(docs));

    const auto list = runChecked({"docs", "list"});
    REQUIRE(list.exitCode == kExitOk);
    bool sawGettingStarted = false;
    bool sawMissingShortcuts = false;
    for (const auto& page : list.answer.at("result").at("pages")) {
        if (page.at("id") == "getting-started") sawGettingStarted = page.at("found").get<bool>();
        if (page.at("id") == "shortcuts") sawMissingShortcuts = !page.at("found").get<bool>();
    }
    CHECK(sawGettingStarted);
    CHECK(sawMissingShortcuts);

    const auto show = runChecked({"docs", "show", "getting-started"});
    REQUIRE(show.exitCode == kExitOk);
    CHECK(show.answer.at("result").at("headings").size() == 2);
    CHECK(show.answer.at("result").at("headings")[1].at("anchor") == "stairs");
    CHECK(errorCode(runChecked({"docs", "show", "shortcuts"})) == "not_found");
    CHECK(errorCode(runChecked({"docs", "show", "nothing"})) == "not_found");

    const auto search = runChecked({"docs", "search", "stairs"});
    REQUIRE(search.exitCode == kExitOk);
    const auto& hits = search.answer.at("result").at("hits");
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].at("heading") == "Stairs");
    CHECK(hits[1].at("page") == "user-guide");
    CHECK(runChecked({"docs", "search", "stairs", "--limit", "1"}).answer.at("result").at("count") == 1);
}

TEST_CASE("samples list and samples open copy only the project files", "[cli][guide][samples]") {
    TempFolder temp("guide-samples");
    const fs::path samples = makeSamples(temp.path());
    const ScopedEnvironment variable("PLYNTH_SAMPLES", utf8(samples));

    const auto list = runChecked({"samples", "list"});
    REQUIRE(list.exitCode == kExitOk);
    CHECK(list.answer.at("result").at("license") == "cc0");
    REQUIRE(list.answer.at("result").at("projects").size() == 1);

    const fs::path into = temp.path() / "projects";
    const auto open = runChecked({"samples", "open", "apartment", "--to", utf8(into)});
    REQUIRE(open.exitCode == kExitOk);
    const fs::path copy = into / "Apartment";
    CHECK(open.answer.at("result").at("project_folder") == utf8(copy));
    CHECK(fs::is_regular_file(copy / "project.json"));
    CHECK(fs::is_regular_file(copy / "plans" / "floor1.png"));
    CHECK_FALSE(fs::exists(copy / "build"));
    const auto again = runChecked({"samples", "open", "apartment", "--to", utf8(into)});
    CHECK(again.answer.at("result").at("project_folder") == utf8(into / "Apartment (2)"));
    CHECK(errorCode(runChecked({"samples", "open", "castle", "--to", utf8(into)})) == "not_found");
    CHECK(errorCode(runChecked({"samples", "open", "apartment", "--to", utf8(samples / "inside")})) == "invalid_value");
}

TEST_CASE("samples add refuses a Blender that is not there", "[cli][guide][samples]") {
    const plynth::cli::testing::GuardedSettingsReset reset;
    TempFolder temp("guide-samples-add");
    const ScopedEnvironment variable("PLYNTH_SAMPLES", utf8(makeSamples(temp.path())));
    REQUIRE(runChecked({"library", "init", utf8(temp.path() / "Library"), "--use"}).exitCode == kExitOk);
    const auto outcome = runChecked({"samples", "add", "--blender", utf8(temp.path() / "no-blender.exe")});
    CHECK(outcome.exitCode != kExitOk);
    CHECK(errorCode(outcome) == "blender_not_found");
}

TEST_CASE("export licenses shows and changes the switch and prints the report of the last build", "[cli][guide][licenses]") {
    const Sample sample("guide-licenses");
    const auto before = sample.run({"export", "licenses"});
    REQUIRE(before.exitCode == kExitOk);
    CHECK(before.answer.at("result").at("enabled") == false);
    CHECK(before.answer.at("result").at("report").is_null());
    CHECK(before.answer.at("result").at("reason") == "Build the project first.");

    const auto on = sample.run({"export", "licenses", "on"});
    REQUIRE(on.exitCode == kExitOk);
    CHECK(on.answer.at("command") == "export licenses on");
    CHECK(on.answer.at("result").at("enabled") == true);
    const auto options = nlohmann::json::parse(readFile(sample.folder / "export-options.json"));
    CHECK(options.at("version") == 4);
    CHECK(options.at("licenses").at("enabled") == true);
    CHECK_FALSE(on.answer.at("result").contains("note"));

    REQUIRE(plynth::writeUsedItems(sample.folder / "build" / "used-items.json", {"abc", {"sofa"}, {}}).ok());
    const auto shown = sample.run({"export", "licenses"});
    REQUIRE(shown.exitCode == kExitOk);
    const auto& report = shown.answer.at("result").at("report");
    REQUIRE(report.is_object());
    CHECK(report.at("plan_fingerprint") == "abc");
    CHECK(report.at("items")[0].at("id") == "sofa");
    CHECK(report.at("items")[0].at("in_library") == false);

    const auto off = sample.run({"export", "licenses", "off"});
    REQUIRE(off.exitCode == kExitOk);
    CHECK_FALSE(fs::exists(sample.folder / "export-options.json"));
    CHECK(errorCode(sample.run({"export", "licenses", "maybe"})) == "bad_arguments");
}

TEST_CASE("help lists the new words", "[cli][guide]") {
    const auto help = runChecked({"help"});
    std::vector<std::string> names;
    for (const auto& command : help.answer.at("result").at("commands")) names.push_back(command.at("command").get<std::string>());
    for (const char* wanted : {"about", "docs list", "docs show", "docs search", "samples list", "samples open", "samples add"}) {
        INFO(wanted);
        CHECK(std::find(names.begin(), names.end(), std::string(wanted)) != names.end());
    }
}
