#include "GuideSupport.h"

#include <plynth/core/Resources.h>

#include <catch2/catch_test_macros.hpp>

using namespace plynth;
using plynth::test::TempFolder;
using plynth::test::writeFile;

namespace {

// Makes a folder of the kind with its marker file.
std::filesystem::path makeMarked(ResourceKind kind, const std::filesystem::path& folder)
{
    writeFile(folder / std::string(resourceMarker(kind)), "x");
    return folder;
}

}

TEST_CASE("Resource kinds have their names and markers", "[resources]")
{
    CHECK(resourceFolderName(ResourceKind::Docs) == "docs");
    CHECK(resourceFolderName(ResourceKind::Samples) == "samples");
    CHECK(resourceFolderName(ResourceKind::Licenses) == "licenses");
    CHECK(resourceMarker(ResourceKind::Docs) == "GETTING_STARTED.md");
    CHECK(resourceMarker(ResourceKind::Samples) == "samples.json");
    CHECK(resourceMarker(ResourceKind::Licenses) == "THIRD_PARTY_NOTICES.md");
    CHECK(resourceVariable(ResourceKind::Docs) == "PLYNTH_DOCS");
    CHECK(resourceVariable(ResourceKind::Samples) == "PLYNTH_SAMPLES");
    CHECK(resourceVariable(ResourceKind::Licenses) == "PLYNTH_LICENSES");
}

TEST_CASE("The variable comes before the program folder before the source tree", "[resources]")
{
    TempFolder root;
    const auto variable = makeMarked(ResourceKind::Docs, root.path() / "chosen");
    const auto program = root.path() / "program";
    const auto source = root.path() / "source";
    makeMarked(ResourceKind::Docs, program / "docs");
    makeMarked(ResourceKind::Docs, source / "docs");

    CHECK(findResourceFolder(ResourceKind::Docs, variable, program, source) == variable);
    CHECK(findResourceFolder(ResourceKind::Docs, {}, program, source) == program / "docs");
    CHECK(findResourceFolder(ResourceKind::Docs, {}, root.path() / "none", source) == source / "docs");
}

TEST_CASE("A folder without its marker is skipped", "[resources]")
{
    TempFolder root;
    const auto variable = root.path() / "chosen";
    std::filesystem::create_directories(variable);
    const auto program = root.path() / "program";
    std::filesystem::create_directories(program / "licenses");
    makeMarked(ResourceKind::Licenses, root.path() / "source" / "licenses");

    CHECK(findResourceFolder(ResourceKind::Licenses, variable, program, root.path() / "source") ==
          root.path() / "source" / "licenses");
    writeFile(program / "licenses" / "GETTING_STARTED.md", "wrong marker");
    CHECK(findResourceFolder(ResourceKind::Licenses, variable, program, {}).empty());
}

TEST_CASE("No parent folder is ever used", "[resources]")
{
    TempFolder root;
    makeMarked(ResourceKind::Samples, root.path() / "samples");
    const auto program = root.path() / "a" / "b";
    std::filesystem::create_directories(program);

    CHECK(findResourceFolder(ResourceKind::Samples, {}, program, {}).empty());
    CHECK(findResourceFolder(ResourceKind::Samples, {}, root.path() / "a", {}).empty());
    CHECK(findResourceFolder(ResourceKind::Samples, {}, root.path(), {}) == root.path() / "samples");
}

TEST_CASE("An empty source tree skips that step", "[resources]")
{
    TempFolder root;
    CHECK(findResourceFolder(ResourceKind::Docs, {}, {}, {}).empty());
    CHECK(findResourceFolder(ResourceKind::Docs, {}, root.path(), {}).empty());
}
