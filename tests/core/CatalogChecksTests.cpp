#include "MaterialCatalogSupport.h"

#include <plynth/core/Generators.h>
#include <plynth/core/catalog/AssetInfo.h>
#include <plynth/core/catalog/CatalogChecks.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// The column generator of the test data, which has one material parameter "material" (default white_paint).
GeneratorInfo columnGenerator()
{
    plynth::test::TempDir tmp("checks-column");
    fs::copy_file(fs::path(PLYNTH_TEST_DATA_DIR) / "generators" / "column.py", tmp.path() / "column.py");
    const GeneratorLibrary library = loadGeneratorFolder(tmp.path());
    const GeneratorInfo* column = library.find("column");
    REQUIRE(column != nullptr);
    return *column;
}

// Writes info.json again with another size and indexes it.
void remeasure(plynth::test::TestLibrary& lib, const std::string& id, AssetSize size)
{
    AssetInfo info = readAssetInfo(lib.paths.assetItems / id / "info.json", id).info;
    info.size = size;
    plynth::test::requireOk(writeAssetInfo(lib.paths.assetItems / id / "info.json", info));
    plynth::test::requireOk(lib.catalog->indexAsset(info));
}

}

TEST_CASE("generator materials are checked against the catalog and name the parameter", "[core][catalogchecks]")
{
    const GeneratorInfo column = columnGenerator();
    plynth::test::MaterialTestLibrary lib("checks-genmat");
    lib.add("brick", "Brick");
    lib.add("slow", "Slow");
    lib.add("gone", "Gone");
    plynth::test::requireOk(lib.catalog->editMaterial("slow", {.tileSize = TileSize{3.0, 3.0}}));
    fs::remove(lib.folder("gone") / "textures" / "normal.png");
    lib.catalog->rescan();
    REQUIRE(lib.catalog->material("gone")->state == MaterialState::Broken);

    const auto one = [](const std::string& value) {
        return std::vector<GeneratorParamValue>{{"material", GeneratorValue{value}}};
    };

    SECTION("a material in the library passes, and so does one that needs a refresh") {
        plynth::test::requireOk(checkGeneratorMaterials(column, one("brick"), lib.catalog.get()));
        plynth::test::requireOk(checkGeneratorMaterials(column, one("slow"), lib.catalog.get()));
    }
    SECTION("empty text is the neutral look and always passes") {
        plynth::test::requireOk(checkGeneratorMaterials(column, one(""), lib.catalog.get()));
        plynth::test::requireOk(checkGeneratorMaterials(column, one(""), nullptr));
    }
    SECTION("a material the library lacks is NotFound with the parameter named") {
        const Status s = checkGeneratorMaterials(column, one("oak_plank"), lib.catalog.get());
        REQUIRE(s.code == ErrorCode::NotFound);
        REQUIRE(s.message == "The material of material: There is no material oak_plank in the library.");
    }
    SECTION("a broken material is refused with its problem") {
        const Status s = checkGeneratorMaterials(column, one("gone"), lib.catalog.get());
        REQUIRE(s.code == ErrorCode::InvalidValue);
        REQUIRE(s.message.rfind("The material of material: The material gone is broken", 0) == 0);
        REQUIRE(s.message.find("textures/normal.png") != std::string::npos);
    }
    SECTION("an id that is not well formed gets the generator's own sentence, with or without a catalog") {
        const Status with = checkGeneratorMaterials(column, one("Not An Id"), lib.catalog.get());
        REQUIRE(with.code == ErrorCode::InvalidValue);
        REQUIRE(with.message == "The value of \"material\" is not a well formed material id.");
        REQUIRE(checkGeneratorMaterials(column, one("Not An Id"), nullptr).message == with.message);
    }
    SECTION("a value that is not text is refused") {
        const std::vector<GeneratorParamValue> number{{"material", GeneratorValue{1.0}}};
        const Status s = checkGeneratorMaterials(column, number, lib.catalog.get());
        REQUIRE(s.code == ErrorCode::InvalidValue);
        REQUIRE(s.message == "The value of \"material\" must be a material id or empty.");
    }
    SECTION("without a catalog a material is refused as the app's picker does") {
        const Status s = checkGeneratorMaterials(column, one("brick"), nullptr);
        REQUIRE(s.code == ErrorCode::NoLibrary);
        REQUIRE(s.message == "The material of material: There is no library with materials.");
    }
    SECTION("only the given values are checked, so a partial set and other kinds pass") {
        plynth::test::requireOk(checkGeneratorMaterials(column, {}, lib.catalog.get()));
        const std::vector<GeneratorParamValue> others{{"diameter", GeneratorValue{0.4}}, {"shape", GeneratorValue{std::string("nonsense")}},
                                                      {"unknown", GeneratorValue{std::string("ghost")}}};
        plynth::test::requireOk(checkGeneratorMaterials(column, others, lib.catalog.get()));
    }
    SECTION("a full set with the default material the library lacks is refused, so callers pass what the user gave") {
        const std::vector<GeneratorParamValue> defaults = defaultGeneratorParams(column, 2.7);
        const Status s = checkGeneratorMaterials(column, defaults, lib.catalog.get());
        REQUIRE(s.code == ErrorCode::NotFound);
        REQUIRE(s.message.find("white_paint") != std::string::npos);
    }
}

TEST_CASE("a frame asset must be a measured, working door or window of the right category", "[core][catalogchecks]")
{
    plynth::test::TestLibrary lib("checks-frame");
    lib.add("door_oak", "Oak door", {"door"});
    lib.add("window_tall", "Tall window", {"window"});
    lib.add("both", "Door and window", {"door", "window"});
    lib.add("sofa", "Sofa", {"sofa"});
    lib.add("door_flat", "Flat door", {"door"});
    remeasure(lib, "door_flat", {0.0, 0.0, 0.0});
    lib.add("door_gone", "Gone door", {"door"});
    fs::remove(lib.paths.assetItems / "door_gone" / "asset.blend");
    lib.add("door_old", "Old door", {"door"});
    plynth::test::writeBytes(lib.paths.assetItems / "door_old" / "asset.blend", "CHANGED-BLEND-FILE");
    lib.catalog->rescan();
    REQUIRE(lib.catalog->asset("door_gone")->state == AssetState::Broken);
    REQUIRE(lib.catalog->asset("door_old")->state == AssetState::NeedsRefresh);

    SECTION("a door asset for a door and a window asset for a window pass") {
        plynth::test::requireOk(checkFrameAsset(*lib.catalog, "door_oak", OpeningType::Door));
        plynth::test::requireOk(checkFrameAsset(*lib.catalog, "window_tall", OpeningType::Window));
        plynth::test::requireOk(checkFrameAsset(*lib.catalog, "both", OpeningType::Door));
        plynth::test::requireOk(checkFrameAsset(*lib.catalog, "both", OpeningType::Window));
    }
    SECTION("an asset that needs a refresh keeps its size and can be fitted") {
        plynth::test::requireOk(checkFrameAsset(*lib.catalog, "door_old", OpeningType::Door));
    }
    SECTION("an unknown asset is NotFound") {
        const Status s = checkFrameAsset(*lib.catalog, "door_99", OpeningType::Door);
        REQUIRE(s.code == ErrorCode::NotFound);
        REQUIRE(s.message == "There is no asset door_99 in the library.");
    }
    SECTION("the wrong category is refused with the CLI's sentence") {
        const Status window = checkFrameAsset(*lib.catalog, "door_oak", OpeningType::Window);
        REQUIRE(window.code == ErrorCode::InvalidValue);
        REQUIRE(window.message == "The asset door_oak is not in the window category, so it cannot fill a window.");
        const Status sofa = checkFrameAsset(*lib.catalog, "sofa", OpeningType::Door);
        REQUIRE(sofa.message == "The asset sofa is not in the door category, so it cannot fill a door.");
    }
    SECTION("a plain opening takes no asset, before the catalog is asked") {
        const Status s = checkFrameAsset(*lib.catalog, "door_99", OpeningType::Plain);
        REQUIRE(s.code == ErrorCode::InvalidValue);
        REQUIRE(s.message == "A plain opening takes no door or window asset. Use --frame auto or generated.");
    }
    SECTION("a broken asset is refused with its problem") {
        const Status s = checkFrameAsset(*lib.catalog, "door_gone", OpeningType::Door);
        REQUIRE(s.code == ErrorCode::InvalidValue);
        REQUIRE(s.message.rfind("Gone door cannot be fitted: ", 0) == 0);
        REQUIRE(s.message.size() > std::string("Gone door cannot be fitted: ").size());
    }
    SECTION("an asset that was never measured is refused") {
        const Status s = checkFrameAsset(*lib.catalog, "door_flat", OpeningType::Door);
        REQUIRE(s.code == ErrorCode::InvalidValue);
        REQUIRE(s.message == "Flat door is not measured yet, so it cannot be fitted. Retry its render first.");
    }
    SECTION("the category is checked before the state") {
        const Status s = checkFrameAsset(*lib.catalog, "door_flat", OpeningType::Window);
        REQUIRE(s.message == "The asset door_flat is not in the window category, so it cannot fill a window.");
    }
}
