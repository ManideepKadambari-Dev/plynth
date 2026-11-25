#include "GuideSupport.h"

#include <plynth/core/Samples.h>

#include <catch2/catch_test_macros.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <set>
#include <string>

using namespace plynth;
using plynth::test::readFile;
using plynth::test::TempFolder;
using plynth::test::utf8Path;
using plynth::test::writeFile;

namespace {

const char* kIndex = R"({"format": "plynth-samples", "version": 1, "license": "cc0",
 "assets": [{"id": "sofa", "name": "Sofa", "file": "models/sofa/sofa.obj", "categories": ["sofa"], "tags": []}],
 "materials": [{"id": "oak_planks", "folder": "materials/oak_planks"}],
 "projects": [{"id": "apartment", "name": "Apartment", "description": "One furnished floor.",
               "folder": "projects/apartment", "image": "projects/apartment/plans/floor1.png"}]})";

// A samples folder with the index and one project that also holds files that must not be copied.
struct SampleFolder {
    TempFolder root;
    std::filesystem::path samples = root.path() / "samples";
    std::filesystem::path project = samples / "projects" / "apartment";

    explicit SampleFolder(std::string_view index = kIndex)
    {
        writeFile(samples / "samples.json", index);
        writeFile(project / "project.json", "{\"project\": 1}\n");
        writeFile(project / "plan.json", "{\"plan\": 1}\n");
        writeFile(project / "export-options.json", "{\"options\": 1}\n");
        writeFile(project / "plans" / "floor1.png", std::string("\x89PNG\r\n\x1a\n", 8) + "pixels");
        writeFile(project / "plans" / "more" / "note.txt", "kept");
        writeFile(project / "build" / "building.blend", "output");
        writeFile(project / "export" / "Flat.fbx", "output");
        writeFile(project / "project.lock", "lock");
        writeFile(project / "app-state.json", "{}");
    }
};

// Every file below a folder, as relative paths with "/".
std::set<std::string> filesBelow(const std::filesystem::path& folder)
{
    std::set<std::string> out;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
        if (entry.is_regular_file()) out.insert(entry.path().lexically_relative(folder).generic_string());
    }
    return out;
}

// The index with one text replaced.
std::string indexWith(std::string_view from, std::string_view to)
{
    std::string text = kIndex;
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}

}

TEST_CASE("A good samples index is read", "[samples]")
{
    SampleFolder f;
    const SamplesRead read = readSamples(f.samples);
    REQUIRE(read.status.ok());
    CHECK(read.index.license == "cc0");
    REQUIRE(read.index.assets.size() == 1);
    CHECK(read.index.assets[0] == SampleAsset{"sofa", "Sofa", "models/sofa/sofa.obj", {"sofa"}, {}});
    REQUIRE(read.index.materials.size() == 1);
    CHECK(read.index.materials[0] == SampleMaterial{"oak_planks", "materials/oak_planks"});
    REQUIRE(read.index.projects.size() == 1);
    CHECK(read.index.projects[0] == SampleProject{"apartment", "Apartment", "One furnished floor.", "projects/apartment",
                                                  "projects/apartment/plans/floor1.png"});
}

TEST_CASE("An index without projects is read", "[samples]")
{
    const SamplesRead read = parseSamples(R"({"format": "plynth-samples", "version": 1, "license": "cc0",
        "assets": [], "materials": []})");
    REQUIRE(read.status.ok());
    CHECK(read.index.projects.empty());
}

TEST_CASE("A bad samples index is refused", "[samples]")
{
    CHECK(parseSamples("not json").status.code == ErrorCode::BadFile);
    CHECK(parseSamples("[]").status.code == ErrorCode::BadFile);
    CHECK(parseSamples(indexWith("plynth-samples", "plynth-tour")).status.where == "/format");
    CHECK(parseSamples(indexWith("\"version\": 1", "\"version\": 2")).status.code == ErrorCode::UnsupportedVersion);
    CHECK(parseSamples(indexWith("\"version\": 1", "\"version\": 0")).status.code == ErrorCode::BadFile);
    CHECK(parseSamples(indexWith("\"version\": 1", "\"version\": \"1\"")).status.code == ErrorCode::BadFile);
    CHECK(parseSamples(indexWith("\"id\": \"sofa\"", "\"id\": \"Sofa\"")).status.where == "/assets/0/id");

    const SamplesRead twice = parseSamples(indexWith("\"materials\": [{\"id\": \"oak_planks\", \"folder\": \"materials/oak_planks\"}]",
        "\"materials\": [{\"id\": \"oak_planks\", \"folder\": \"materials/a\"}, {\"id\": \"oak_planks\", \"folder\": \"materials/b\"}]"));
    CHECK(twice.status.code == ErrorCode::BadFile);
    CHECK(twice.status.where == "/materials/1/id");

    for (const char* bad : {"../outside/sofa.obj", "models/../../x.obj", "C:/models/sofa.obj", "/models/sofa.obj",
                            "\\\\server\\sofa.obj", "models//sofa.obj", "./models/sofa.obj"}) {
        INFO(bad);
        std::string path = bad;
        std::string escaped;
        for (char c : path) {
            if (c == '\\') escaped += "\\\\";
            else escaped.push_back(c);
        }
        const SamplesRead read = parseSamples(indexWith("models/sofa/sofa.obj\"", escaped + "\""));
        CHECK(read.status.code == ErrorCode::BadFile);
        CHECK(read.status.where == "/assets/0/file");
    }
}

TEST_CASE("A missing index cannot be read", "[samples]")
{
    TempFolder root;
    CHECK(readSamples(root.path()).status.code == ErrorCode::CannotRead);
}

TEST_CASE("A sample copy holds only the project files", "[samples]")
{
    SampleFolder f;
    const auto into = f.root.path() / "projects";
    const SampleCopy copy = copySampleProject(f.samples, "apartment", into);
    REQUIRE(copy.status.ok());
    CHECK(copy.projectFolder == into / "Apartment");
    CHECK(filesBelow(copy.projectFolder) == std::set<std::string>{"project.json", "plan.json", "export-options.json",
                                                                  "plans/floor1.png", "plans/more/note.txt"});
    CHECK(readFile(copy.projectFolder / "plans" / "floor1.png") == readFile(f.project / "plans" / "floor1.png"));
}

TEST_CASE("A sample copy gets AGENTS.md from the master file and never loses its own", "[samples]")
{
    SampleFolder f;
    const auto master = f.root.path() / "docs" / "templates" / "project-agents.md";
    writeFile(master, "rules
");
    const auto into = f.root.path() / "projects";
    const SampleCopy copy = copySampleProject(f.samples, "apartment", into, master);
    REQUIRE(copy.status.ok());
    CHECK(readFile(copy.projectFolder / "AGENTS.md") == "rules
");
    CHECK(filesBelow(copy.projectFolder).count("AGENTS.md") == 1);

    writeFile(f.project / "AGENTS.md", "from the sample
");
    const SampleCopy second = copySampleProject(f.samples, "apartment", into, master);
    REQUIRE(second.status.ok());
    CHECK(readFile(second.projectFolder / "AGENTS.md") == "rules
");

    const SampleCopy none = copySampleProject(f.samples, "apartment", into, f.root.path() / "missing.md");
    REQUIRE(none.status.ok());
    CHECK(filesBelow(none.projectFolder).count("AGENTS.md") == 0);
}

TEST_CASE("A second copy gets a number and the source stays as it was", "[samples]")
{
    SampleFolder f;
    const auto before = filesBelow(f.samples);
    const std::string plan = readFile(f.project / "plan.json");
    const auto into = f.root.path() / "projects";
    const SampleCopy first = copySampleProject(f.samples, "apartment", into);
    const SampleCopy second = copySampleProject(f.samples, "apartment", into);
    REQUIRE(first.status.ok());
    REQUIRE(second.status.ok());
    CHECK(second.projectFolder == into / "Apartment (2)");
    CHECK(filesBelow(f.samples) == before);
    CHECK(readFile(f.project / "plan.json") == plan);
}

TEST_CASE("An unknown sample or a target inside the samples is refused", "[samples]")
{
    SampleFolder f;
    CHECK(copySampleProject(f.samples, "castle", f.root.path() / "projects").status.code == ErrorCode::NotFound);
    CHECK(copySampleProject(f.samples, "apartment", f.samples / "out").status.code == ErrorCode::InvalidValue);
    CHECK(copySampleProject(f.samples, "apartment", f.samples).status.code == ErrorCode::InvalidValue);
    CHECK(!std::filesystem::exists(f.samples / "out"));
}

TEST_CASE("A failed copy leaves nothing behind", "[samples]")
{
    SampleFolder f;
    const auto into = f.root.path() / "projects";
    std::filesystem::create_directories(into);
    const std::wstring locked = (f.project / "plans" / "floor1.png").wstring();
    HANDLE handle = CreateFileW(locked.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(handle != INVALID_HANDLE_VALUE);
    const SampleCopy copy = copySampleProject(f.samples, "apartment", into);
    CloseHandle(handle);
    CHECK(copy.status.code == ErrorCode::CannotWrite);
    CHECK(std::filesystem::is_empty(into));
}

TEST_CASE("Sample copies work with Unicode names", "[samples]")
{
    SampleFolder f(indexWith("\"name\": \"Apartment\"", "\"name\": \"Wohnung \xC3\x84 \xE5\xAE\xB6\""));
    const auto into = f.root.path() / utf8Path("Projekte \xC3\xB6");
    const SampleCopy copy = copySampleProject(f.samples, "apartment", into);
    REQUIRE(copy.status.ok());
    CHECK(copy.projectFolder == into / utf8Path("Wohnung \xC3\x84 \xE5\xAE\xB6"));
    CHECK(std::filesystem::is_regular_file(copy.projectFolder / "project.json"));
}

TEST_CASE("The samples index example validates against its schema", "[samples]")
{
    CHECK(plynth::test::schemaProblem("samples.schema.json", nlohmann::json::parse(kIndex)).empty());
    CHECK(!plynth::test::schemaProblem("samples.schema.json", nlohmann::json::parse(indexWith("plynth-samples", "x"))).empty());
}

TEST_CASE("The shipped samples index lists every sample model and material", "[samples]")
{
    const std::filesystem::path samples = PLYNTH_SAMPLES_DIR;
    const SamplesRead read = readSamples(samples);
    REQUIRE(read.status.ok());
    CHECK(read.index.license == "cc0");
    CHECK(plynth::test::schemaProblem("samples.schema.json",
                                      nlohmann::json::parse(readFile(samples / "samples.json"))).empty());

    std::set<std::string> listedModels;
    for (const SampleAsset& asset : read.index.assets) {
        INFO(asset.file);
        CHECK(std::filesystem::is_regular_file(samples / utf8Path(asset.file)));
        listedModels.insert(utf8Path(asset.file).parent_path().filename().string());
    }
    std::set<std::string> listedMaterials;
    for (const SampleMaterial& material : read.index.materials) {
        INFO(material.folder);
        CHECK(std::filesystem::is_regular_file(samples / utf8Path(material.folder) / "material.json"));
        listedMaterials.insert(utf8Path(material.folder).filename().string());
    }
    for (const SampleProject& project : read.index.projects) {
        INFO(project.folder);
        CHECK(std::filesystem::is_regular_file(samples / utf8Path(project.folder) / "project.json"));
        CHECK(std::filesystem::is_regular_file(samples / utf8Path(project.image)));
    }

    std::set<std::string> modelFolders;
    // A model folder holds an .obj file. Other folders, such as a Python cache, are not models.
    for (const auto& entry : std::filesystem::directory_iterator(samples / "models")) {
        if (!entry.is_directory()) continue;
        for (const auto& file : std::filesystem::directory_iterator(entry.path())) {
            if (file.path().extension() == ".obj") {
                modelFolders.insert(entry.path().filename().string());
                break;
            }
        }
    }
    std::set<std::string> materialFolders;
    for (const auto& entry : std::filesystem::directory_iterator(samples / "materials")) {
        if (entry.is_directory() && std::filesystem::is_regular_file(entry.path() / "material.json")) {
            materialFolders.insert(entry.path().filename().string());
        }
    }
    CHECK(listedModels == modelFolders);
    CHECK(listedMaterials == materialFolders);
}
