#include "BuildFixtures.h"
#include "CatalogSupport.h"
#include "FakeBlender.h"
#include "SquareFixtures.h"

#include <plynth/core/Build.h>
#include <plynth/core/BuildResolve.h>
#include <plynth/core/catalog/BuildChoices.h>
#include <plynth/core/Pack.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/AssetMaterials.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

using namespace plynth;
using namespace plynth::test;
namespace fs = std::filesystem;

namespace {

// Seconds since 1970 now.
long long epochNow()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// Says whether contents name a use.
bool uses(const PackContents& contents, PackKind kind, const std::string& id)
{
    return std::any_of(contents.used.begin(), contents.used.end(),
                       [&](const PackUse& u) { return u.kind == kind && u.id == id; });
}

}

TEST_CASE("requestCancel writes the marker only while a job holds the lock", "[gaps]") {
    TempDir tmp("gaps-cancel");
    const fs::path project = tmp.path() / "P";
    fs::create_directories(project / "build");
    CHECK_FALSE(requestCancel(project));
    CHECK_FALSE(fs::exists(project / "build" / "cancel.request"));

    std::ofstream(project / "build" / "build.lock") << 999999 << " " << 1 << " " << epochNow() - 100000 << "\n";
    CHECK_FALSE(requestCancel(project));

    const ProcessIdentity me = currentProcess();
    std::ofstream(project / "build" / "build.lock", std::ios::trunc) << me.pid << " " << me.startKey << " " << epochNow() << "\n";
    CHECK(requestCancel(project));
    CHECK(fs::exists(project / "build" / "cancel.request"));
}

TEST_CASE("a cancel request stops a running build and leaves no marker for the next one", "[gaps]") {
    TestLibrary lib("gaps-build-cancel");
    const fs::path project = lib.tmp.path() / "Project";
    fs::create_directories(project);
    BuildRequest request;
    request.projectFolder = project;
    request.project = buildProject();
    clearMaterialDefaults(request.project);
    request.library = lib.paths;
    request.blender = inspectBlender(makeFakeBlender(lib.tmp.path() / "blender", "5.2.0", "hang"));
    request.run.scriptsFolder = makeFakeScripts(lib.tmp.path() / "scripts");
    lib.add("sofa", "Sofa", {"sofa"});

    fs::create_directories(project / "build");
    std::ofstream(project / "build" / "cancel.request") << "left from before";
    std::atomic<bool> done = false;
    // Waits until the job's Blender has started, then asks once and never again. The fake job hangs until it is
    // stopped, so the build cannot end between the request and the marker being seen, and no request can land
    // after the build removed the marker. If the job never starts within 30 seconds it asks anyway, so the test
    // ends with failed checks instead of hanging.
    const fs::path argsLog = lib.tmp.path() / "blender" / "fake_args.log";
    const auto jobStarted = [&]() {
        std::ifstream in(argsLog, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return text.find("--job") != std::string::npos;
    };
    std::thread canceller([&] {
        for (int i = 0; i < 300 && !done && !jobStarted(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        for (int i = 0; i < 300 && !done; ++i) {
            if (requestCancel(project)) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    const BuildReport report = runBuild(request);
    done = true;
    canceller.join();
    CHECK(report.status.code == ErrorCode::Cancelled);
    CHECK_FALSE(fs::exists(project / "build" / "cancel.request"));
    CHECK_FALSE(fs::exists(project / "build" / "build.lock"));

    // Every start of Blender, the version check and the job, keeps scripts inside .blend files from running.
    std::ifstream log(lib.tmp.path() / "blender" / "fake_args.log", std::ios::binary);
    std::string line;
    int starts = 0;
    while (std::getline(log, line)) {
        if (line.find_first_not_of('\r') == std::string::npos) continue;
        ++starts;
        INFO(line);
        CHECK(line.find("--disable-autoexec") != std::string::npos);
    }
    CHECK(starts >= 2);
}

TEST_CASE("recoverAutosave saves the autosave or throws it away", "[gaps]") {
    TempDir tmp("gaps-recover");
    const fs::path folder = tmp.path() / "P";
    Project saved = squareProjectWithWalls();
    requireOk(saveProject(saved, folder, {}));
    CHECK(recoverAutosave(folder, false).code == ErrorCode::NotFound);
    CHECK(recoverAutosave(tmp.path(), false).code == ErrorCode::NotAProject);

    Project changed = saved;
    changed.plan.floors[0].walls[0].thickness = 0.35;
    requireOk(writeAutosave(changed, folder));
    requireOk(recoverAutosave(folder, true));
    CHECK_FALSE(findAutosave(folder).exists);
    CHECK(loadProject(folder).project.plan.floors[0].walls[0].thickness == 0.2);

    requireOk(writeAutosave(changed, folder));
    requireOk(recoverAutosave(folder, false));
    CHECK_FALSE(findAutosave(folder).exists);
    CHECK(loadProject(folder).project.plan.floors[0].walls[0].thickness == 0.35);
}

TEST_CASE("a material taken from an asset keeps the asset's source and license", "[gaps]") {
    TestLibrary lib("gaps-asset-material");
    AssetInfo info = makeAssetOnDisk(lib.paths, "chair", "Chair");
    info.materials = {"Seat", "Legs"};
    info.source = {"blenderkit", "https://www.blenderkit.com/get/abc", sessionNow()};
    info.license = "royalty_free";
    requireOk(writeAssetInfo(lib.paths.assetItems / "chair" / "info.json", info));

    const AssetMaterialRequest made = assetMaterialRequest(lib.paths, "chair", "Seat", {"fabric"});
    requireOk(made.status);
    CHECK(made.request.blendMaterial == "Seat");
    CHECK(made.request.source.file == lib.paths.assetItems / "chair" / "asset.blend");
    CHECK(made.request.license == "royalty_free");
    REQUIRE(made.request.origin.has_value());
    CHECK(made.request.origin->kind == "blenderkit");
    CHECK(made.request.origin->reference == "https://www.blenderkit.com/get/abc");
    CHECK(made.request.groups == std::vector<std::string>{"fabric"});
    CHECK(packCopyRule(PackKind::Material, *made.request.origin, made.request.license) == PackReason::Store);

    const AssetMaterialRequest noMaterial = assetMaterialRequest(lib.paths, "chair", "Cushion");
    CHECK(noMaterial.status.code == ErrorCode::NotFound);
    CHECK(noMaterial.status.message.find("Seat") != std::string::npos);
    CHECK(assetMaterialRequest(lib.paths, "ghost", "Seat").status.code == ErrorCode::NotFound);
}

TEST_CASE("the sample material requests cover every sample folder once and skip existing ones", "[gaps]") {
    const fs::path samples = fs::path(PLYNTH_SAMPLES_DIR) / "materials";
    const std::vector<MaterialImportRequest> requests = sampleMaterialRequests(samples);
    REQUIRE_FALSE(requests.empty());
    std::vector<fs::path> folders;
    for (const auto& entry : fs::directory_iterator(samples)) {
        if (entry.is_directory()) folders.push_back(entry.path());
    }
    std::sort(folders.begin(), folders.end());
    REQUIRE(requests.size() == folders.size());
    for (std::size_t i = 0; i < requests.size(); ++i) {
        CHECK(requests[i].source.file == folders[i]);
        CHECK(requests[i].skipExisting);
    }
    CHECK(sampleMaterialRequests(samples / "missing").empty());
}

TEST_CASE("packContents lists what the marked project uses, each once", "[gaps][pack]") {
    TestLibrary lib("gaps-contents");
    requireOk(lib.catalog->addRoomType("gaming", "Gaming room"));
    Project project = squareProjectWithWalls();
    Floor& floor = project.plan.floors[0];
    floor.items.push_back(buildItem("i1", "sofa", 1, 1, {1, 1, 1}));
    floor.items.push_back(buildItem("i2", "sofa", 2, 2, {1, 1, 1}));
    Item generated = buildItem("i3", "", 3, 3, {1, 1, 1});
    generated.generator = GeneratorUse{"spiral", {}};
    floor.items.push_back(generated);
    Opening door = doorOn("w1", 2.0);
    door.id = "o1";
    door.shape = "trefoil";
    floor.openings.push_back(door);
    Opening plain = doorOn("w2", 2.0);
    plain.id = "o2";
    floor.openings.push_back(plain);
    Room room;
    room.id = "r1";
    room.type = "gaming";
    floor.rooms.push_back(room);
    Room other;
    other.id = "r2";
    other.type = "kitchen";
    floor.rooms.push_back(other);
    project.settings.materials.floor = std::string("my_floor");

    const PackContents contents = packContents(project, lib.paths);
    CHECK(uses(contents, PackKind::Asset, "sofa"));
    CHECK(std::count_if(contents.used.begin(), contents.used.end(), [](const PackUse& u) { return u.id == "sofa"; }) == 1);
    CHECK(uses(contents, PackKind::Generator, "spiral"));
    CHECK_FALSE(uses(contents, PackKind::Asset, ""));
    CHECK(uses(contents, PackKind::Shape, "trefoil"));
    CHECK_FALSE(uses(contents, PackKind::Shape, "rectangle"));
    CHECK(uses(contents, PackKind::Material, "my_floor"));
    REQUIRE(contents.roomTypes.size() == 1);
    CHECK(contents.roomTypes[0].id == "gaming");
    CHECK(contents.roomTypes[0].name == "Gaming room");
}

TEST_CASE("packContents holds what a seed picks as well as what is marked", "[gaps][pack][resolved]") {
    TestLibrary lib("gaps-resolved");
    lib.add("sofa_a", "Sofa A", {"sofa"});
    lib.add("sofa_b", "Sofa B", {"sofa"});
    lib.add("sofa_c", "Sofa C", {"sofa"});
    Project project = squareProjectWithWalls();
    project.plan.floors[0].items.push_back(buildItem("i1", "sofa_a", 2, 2, {0.5, 0.5, 0.9}));

    const BuildChoices choices = buildChoicesFrom(lib.catalog.get(), nullptr);
    REQUIRE(choices.assets.size() == 3);
    bool varied = false;
    for (std::uint32_t seed : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u}) {
        INFO("seed " << seed);
        project.settings.variation.seed = seed;
        const ResolvedBuild resolved = resolveBuild(project, choices);
        const PackContents contents = packContents(project, lib.paths);
        CHECK(uses(contents, PackKind::Asset, "sofa_a"));
        for (const Floor& floor : resolved.project.plan.floors) {
            for (const Item& item : floor.items) {
                if (item.asset.empty()) continue;
                CHECK(uses(contents, PackKind::Asset, item.asset));
                varied = varied || item.asset != "sofa_a";
            }
        }
        for (const FloorDressing& floor : resolved.dressing) {
            for (const DressingPlacement& prop : floor.props) CHECK(uses(contents, PackKind::Asset, prop.asset));
        }
        if (seed == 0) {
            CHECK_FALSE(uses(contents, PackKind::Asset, "sofa_b"));
            CHECK_FALSE(uses(contents, PackKind::Asset, "sofa_c"));
        }
    }
    CHECK(varied);
}

TEST_CASE("packContents names a custom trim profile and not the built-in ones", "[gaps][pack][resolved]") {
    TestLibrary lib("gaps-trim");
    Project project = squareProjectWithWalls();
    project.settings.trim.skirting.profile = "ogee";
    project.settings.trim.cornice.enabled = false;
    project.settings.trim.cornice.profile = "wave";
    const PackContents contents = packContents(project, lib.paths);
    CHECK(uses(contents, PackKind::TrimProfile, "ogee"));
    CHECK_FALSE(uses(contents, PackKind::TrimProfile, "wave"));
    CHECK_FALSE(uses(contents, PackKind::TrimProfile, "bevel"));
    CHECK_FALSE(uses(contents, PackKind::TrimProfile, "rounded"));
}
