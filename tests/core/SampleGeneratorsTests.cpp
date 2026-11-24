#include "FakeBlender.h"
#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/Generators.h>
#include <plynth/core/Library.h>
#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
namespace fs = std::filesystem;

namespace {

// A made-up user with an empty samples folder named by PLYNTH_SAMPLES.
struct SampleScratch {
    explicit SampleScratch(const std::string& tag) : user(tag), root(user.tmp.path()), samples(root / "samples")
    {
        fs::create_directories(samples);
        for (const char* name : {"local", "home", "xdg", "settings"}) fs::create_directories(root / name);
        set("LOCALAPPDATA", root / "local");
        set("HOME", root / "home");
        set("XDG_DATA_HOME", root / "xdg");
        set("PLYNTH_CONFIG_DIR", root / "settings");
        set("PLYNTH_BLENDER", root / "no-blender.exe");
        set("PLYNTH_SAMPLES", samples);
    }

    // Points one variable at a path of this test.
    void set(const char* name, const fs::path& value)
    {
        envs.push_back(std::make_unique<ScopedEnv>(name, pathToUtf8(value)));
    }

    // Writes one file in the samples folder.
    void write(const fs::path& name, const std::string& text) const
    {
        fs::create_directories((samples / name).parent_path());
        std::ofstream(samples / name, std::ios::binary | std::ios::trunc) << text;
    }

    IsolatedUser user;
    fs::path root;
    fs::path samples;
    std::vector<std::unique_ptr<ScopedEnv>> envs;
};

// What is found without the variable: the folder beside the program, else the one of a developer build.
fs::path withoutVariable()
{
    wchar_t program[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, program, static_cast<DWORD>(std::size(program)));
    if (length > 0 && length < std::size(program)) {
        const fs::path beside = fs::path(program).parent_path() / "samples" / "generators";
        if (fs::is_directory(beside)) return beside;
    }
#if PLYNTH_DEV_SAMPLES
    return fs::path(PLYNTH_SAMPLES_DIR) / "generators";
#else
    return {};
#endif
}

// Checks that the folder found is the one expected, or that none is.
void requireFound(const fs::path& expected)
{
    const fs::path found = sampleGeneratorsFolder();
    INFO(pathToUtf8(found));
    if (expected.empty()) {
        REQUIRE(found.empty());
    } else {
        REQUIRE_FALSE(found.empty());
        REQUIRE(fs::equivalent(found, expected));
    }
}

// The names of the files directly in a folder, sorted.
std::vector<std::string> fileNames(const fs::path& folder)
{
    std::vector<std::string> names;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file()) names.push_back(pathToUtf8(it->path().filename()));
    }
    std::sort(names.begin(), names.end());
    return names;
}

}

TEST_CASE("sampleGeneratorsFolder takes the folder of PLYNTH_SAMPLES first", "[core][library][samplegenerators]")
{
    SampleScratch s("samplegen-variable");
    fs::create_directories(s.samples / "generators");

    SECTION("with files") { s.write("generators/mine.py", "x = 1\n"); }
    SECTION("empty") {}
    requireFound(s.samples / "generators");
}

TEST_CASE("sampleGeneratorsFolder falls back like the sample shapes", "[core][library][samplegenerators]")
{
    SampleScratch s("samplegen-fallback");
    std::unique_ptr<ScopedEnv> variable;

    SECTION("only shapes in the variable") { s.write("shapes/oval.shape.json", "{}"); }
    SECTION("a file in its place") { s.write("generators", "not a folder"); }
    SECTION("a folder that is not there")
    {
        variable = std::make_unique<ScopedEnv>("PLYNTH_SAMPLES", pathToUtf8(s.root / "none"));
    }
    SECTION("no variable") { variable = std::make_unique<ScopedEnv>("PLYNTH_SAMPLES", ""); }
    requireFound(withoutVariable());
}

TEST_CASE("initLibrary copies the sample generators and never writes over one", "[core][library][samplegenerators]")
{
    SampleScratch s("samplegen-copy");
    fs::create_directories(s.samples / "shapes");
    s.write("generators/mine.py", "mine = 1\n");
    s.write("generators/other.py", "other = 1\n");
    s.write("generators/_helper.py", "helper = 1\n");
    s.write("generators/README.md", "read me");
    s.write("generators/__pycache__/mine.pyc", "cached");
    s.write("generators/inner/deep.py", "deep = 1\n");

    const fs::path root = s.root / "Library";
    requireOk(initLibrary(root));
    const fs::path made = libraryPaths(root).generators;
    REQUIRE(fileNames(made) == std::vector<std::string>{"_helper.py", "mine.py", "other.py"});
    REQUIRE(readTextFile(made / "mine.py") == "mine = 1\n");
    REQUIRE_FALSE(fs::exists(made / "__pycache__"));
    REQUIRE_FALSE(fs::exists(made / "inner"));

    std::ofstream(made / "mine.py", std::ios::binary | std::ios::trunc) << "edited";
    std::ofstream(made / "own.py", std::ios::binary) << "own";
    fs::remove(made / "other.py");
    requireOk(initLibrary(root));
    REQUIRE(readTextFile(made / "mine.py") == "edited");
    REQUIRE(readTextFile(made / "own.py") == "own");
    REQUIRE(readTextFile(made / "other.py") == "other = 1\n");
}

TEST_CASE("initLibrary succeeds without samples", "[core][library][samplegenerators]")
{
    SampleScratch s("samplegen-none");
    std::vector<std::string> expected;

    SECTION("both folders empty")
    {
        fs::create_directories(s.samples / "shapes");
        fs::create_directories(s.samples / "generators");
    }
    SECTION("neither folder")
    {
        if (const fs::path fallback = withoutVariable(); !fallback.empty()) {
            for (const std::string& name : fileNames(fallback)) {
                if (fs::path(name).extension() == ".py") expected.push_back(name);
            }
        }
    }
    const fs::path root = s.root / "Library";
    requireOk(initLibrary(root));
    REQUIRE(isLibraryFolder(root));
    REQUIRE(fileNames(libraryPaths(root).generators) == expected);
}

TEST_CASE("missing samples of one kind do not stop the other", "[core][library][samplegenerators]")
{
    SampleScratch s("samplegen-kinds");
    const fs::path root = s.root / "Library";

    SECTION("no shapes")
    {
        s.write("generators/mine.py", "mine = 1\n");
        requireOk(initLibrary(root));
        REQUIRE(fs::exists(libraryPaths(root).generators / "mine.py"));
    }
    SECTION("no generators")
    {
        s.write("shapes/oval.shape.json", "{}");
        requireOk(initLibrary(root));
        REQUIRE(fs::exists(libraryPaths(root).shapes / "oval.shape.json"));
    }
}

#if PLYNTH_DEV_SAMPLES
TEST_CASE("the six samples land in a new library and all read", "[core][library][samplegenerators]")
{
    SampleScratch s("samplegen-six");
    const ScopedEnv variable("PLYNTH_SAMPLES", pathToUtf8(fs::path(PLYNTH_SAMPLES_DIR)));
    const fs::path root = s.root / "Library";
    requireOk(initLibrary(root));
    const GeneratorLibrary library = loadGeneratorFolder(libraryPaths(root).generators);
    REQUIRE(library.refused.empty());
    std::vector<std::string> ids;
    for (const GeneratorInfo& info : library.generators) ids.push_back(info.id);
    const std::vector<std::string> wanted{"archimesh_column", "bagapie_fence", "bool_tool_screen",
                                          "column", "home_builder_cabinets", "railing"};
    REQUIRE(ids == wanted);
}
#endif
