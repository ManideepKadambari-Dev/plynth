#include "TestSupport.h"
#include "ZipTestWriter.h"

#include <plynth/core/catalog/FabZip.h>
#include <plynth/core/catalog/Importer.h>
#include <plynth/core/catalog/MaterialImporter.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace plynth;

namespace {

// Zip entries with these names.
std::vector<ZipEntry> listed(const std::vector<std::string>& names)
{
    std::vector<ZipEntry> out;
    for (const std::string& name : names) {
        ZipEntry entry;
        entry.name = name;
        entry.size = 1;
        entry.compressedSize = 1;
        out.push_back(entry);
    }
    return out;
}

// An item: its model and the three textures that go with it.
struct ZipItem {
    std::string model;
    std::vector<std::string> textures;
};

// Items whose own names hold a word that also names previews.
std::vector<ZipItem> items()
{
    std::vector<ZipItem> out;
    for (const std::string& title : {"Iron_Manhole_Cover_abcdefg", "Cast_Render_Wall_qwertyu", "BedCover_Linen", "Cover_Plate"}) {
        ZipItem item{title + "_High.fbx", {}};
        for (const char* channel : {"BaseColor", "Normal", "Roughness"}) item.textures.push_back(title + "_High_4K_" + channel + ".jpg");
        out.push_back(item);
    }
    for (const std::string& folder : {"Manhole_Cover", "Render_Wall"}) {
        ZipItem item{folder + "/Piece.fbx", {}};
        for (const char* channel : {"BaseColor", "Normal", "Roughness"}) item.textures.push_back(folder + "/Textures/Piece_" + channel + ".png");
        out.push_back(item);
    }
    for (ZipItem& item : out) std::sort(item.textures.begin(), item.textures.end());
    return out;
}

// The default name the asset importer plans for a zip with these entries.
std::string assetName(std::initializer_list<const char*> names)
{
    plynth::test::TempDir tmp("na");
    ImportSource source;
    source.file = tmp.path() / "item.zip";
    plynth::test::writeZipFile(source.file, plynth::test::zipEntriesNamed(names));
    ImportPlan plan;
    plynth::test::requireOk(findImporter(kFabImporterId)->plan(source, tmp.path(), plan));
    return plan.defaultName;
}

// The default name the material importer plans for a zip of this name.
std::string materialName(const std::string& zip, std::initializer_list<const char*> names)
{
    plynth::test::TempDir tmp("nm");
    ImportSource source;
    source.file = tmp.path() / zip;
    plynth::test::writeZipFile(source.file, plynth::test::zipEntriesNamed(names));
    MaterialImportPlan plan;
    plynth::test::requireOk(findMaterialImporter(kFabImporterId)->plan(source, tmp.path(), plan));
    return plan.defaultName;
}

}

TEST_CASE("a word of the item's name does not make its textures previews", "[fab][store_words]")
{
    for (const ZipItem& item : items()) {
        INFO(item.model);
        std::vector<std::string> names = item.textures;
        names.push_back(item.model);
        const FabZipContents model = classifyFabZip(listed(names));
        CHECK(model.previews.empty());
        const FabChoice chosen = chooseFabModel(model, FabOptions{});
        CHECK(chosen.status.ok());
        CHECK(chosen.textures == item.textures);

        const FabZipContents material = classifyFabZip(listed(item.textures));
        CHECK(material.previews.empty());
        CHECK(chooseFabMaterial(material, FabOptions{}).textures == item.textures);
    }
}

TEST_CASE("real previews stay previews", "[fab][store_words]")
{
    const std::vector<std::string> previews{"Manhole_Cover/Previews/front.png", "Previews/Chair_BaseColor.png", "Previews/front.png",
                                            "Chair_Render.png", "chair_thumb.png", "cover.jpg"};
    for (const std::string& name : previews) {
        INFO(name);
        const FabZipContents contents = classifyFabZip(listed({name}));
        CHECK(contents.previews == std::vector<std::string>{name});
        CHECK(contents.textures.empty());
    }
}

TEST_CASE("a last word is an item id only with evidence", "[fab_importer][store_names]")
{
    CHECK(assetName({"Old_wooden_cabinet.fbx"}) == "Old wooden cabinet");
    CHECK(assetName({"Rusty_barrel.fbx"}) == "Rusty barrel");
    CHECK(assetName({"Stone_statue_High.fbx", "unrelated.json"}) == "Stone statue");
    CHECK(materialName("Stone_statue.zip", {"Stone_BaseColor.png"}) == "Stone statue");
    CHECK(materialName("Old_marble.zip", {"Marble_BaseColor.png"}) == "Old marble");
}

TEST_CASE("an item id with evidence is still dropped", "[fab_importer][store_names]")
{
    CHECK(assetName({"Rusty_Metal_abcdefg_High.fbx", "abcdefg.json"}) == "Rusty Metal");
    CHECK(materialName("Rusty_Metal_abcdefg_High.zip", {"Rusty_Metal_abcdefg_High_4K_BaseColor.jpg", "abcdefg.json"}) == "Rusty Metal");
}
