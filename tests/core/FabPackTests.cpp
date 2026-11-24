#include "StoreTestRig.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace plynth;
using plynth::test::AssetRig;
namespace fs = std::filesystem;

namespace {

using Names = std::vector<std::string>;

// A pack, two words that tell its models apart and whether the zip is taken as Fab by itself.
struct Pack {
    Names names;
    const char* first;
    const char* second;
    bool guessed;
};

const std::vector<Pack> kPacks{
    {{"Chair/model.fbx", "Table/model.fbx"}, "Chair", "Table", true},
    {{"Chair/scene.gltf", "Table/scene.gltf"}, "Chair", "Table", true},
    {{"Pack/Chair/model.fbx", "Pack/Table/model.fbx"}, "Chair", "Table", true},
    {{"Pack/Chair/Meshes/model.fbx", "Pack/Table/Meshes/model.fbx"}, "Chair", "Table", true},
    {{"Pack/Chair/FBX/model.fbx", "Pack/Table/FBX/model.fbx"}, "Chair", "Table", true},
    {{"Chair/model.fbx", "Table/scene.gltf"}, "Chair", "Table", false},
    {{"Red/Chair.fbx", "Blue/Chair.fbx"}, "Red", "Blue", true},
    {{"Set/Red/Chair_LOD0.fbx", "Set/Blue/Chair_LOD0.fbx"}, "Red", "Blue", true},
    {{"Red/Chair.fbx", "Blue/Chair.obj"}, "Red", "Blue", true}};

// Says whether a refusal names both models.
bool namesBoth(const std::string& message, const Pack& pack)
{
    return message.find("holds several models") != std::string::npos && message.find(pack.first) != std::string::npos &&
           message.find(pack.second) != std::string::npos;
}

}

TEST_CASE("models of different folders with one file name are a pack", "[fab][store_pack]")
{
    for (const Pack& pack : kPacks) {
        INFO(pack.names[0] << " and " << pack.names[1]);
        const FabZipContents contents = plynth::test::fabContentsOf(pack.names);
        const FabChoice refused = chooseFabModel(contents, FabOptions{});
        CHECK(refused.status.code == ErrorCode::InvalidValue);
        CHECK(namesBoth(refused.status.message, pack));
        CHECK(refused.otherStems.size() == 2);

        FabOptions options;
        options.model = pack.names[1];
        const FabChoice picked = chooseFabModel(contents, options);
        plynth::test::requireOk(picked.status);
        REQUIRE(picked.model.has_value());
        CHECK(picked.model->entry == pack.names[1]);
        CHECK(picked.otherStems.size() == 1);
    }
}

TEST_CASE("a pack is refused before Blender and its zip stays", "[store_import][store_pack]")
{
    for (const Pack& pack : kPacks) {
        INFO(pack.names[0] << " and " << pack.names[1]);
        AssetRig rig("pk");
        ImportRequest request;
        request.source.file = rig.zip("pack.zip", pack.names);
        if (!pack.guessed) request.importer = std::string(kFabImporterId);
        const ImportOutcome refused = importAsset(*rig.lib.catalog, rig.context, request);
        CHECK(refused.status.code == ErrorCode::InvalidValue);
        CHECK(namesBoth(refused.status.message, pack));
        CHECK(refused.details.value("models", Json::array()).size() == 2);
        CHECK(rig.jobs().empty());
        CHECK(fs::exists(request.source.file));

        if (pack.guessed) {
            AssetRig inbox("pi");
            const fs::path entry = inbox.inboxZip("pack.zip", pack.names);
            CHECK(importInboxEntry(*inbox.lib.catalog, inbox.context, entry).status.code == ErrorCode::InvalidValue);
            CHECK(fs::exists(inbox.lib.paths.assetInboxFailed / "pack.zip"));
            CHECK(inbox.jobs().empty());
        }
        if (!fs::exists(request.source.file)) continue;
        request.importerOptions = Json{{"model", pack.names[1]}};
        request.keepOriginal = true;
        plynth::test::requireOk(importAsset(*rig.lib.catalog, rig.context, request).status);
    }
}

TEST_CASE("versions of one model are not a pack", "[fab][store_pack]")
{
    for (const Names& names : {Names{"FBX/Low/Chair.fbx", "FBX/High/Chair.fbx"},
                               Names{"Chair/FBX/Medium/Chair_LOD0.fbx", "Chair/FBX/Medium/Chair_LOD1.fbx", "Chair/glTF/Medium/Chair.gltf"},
                               Names{"Chair.fbx", "Raw/Chair.fbx"}, Names{"Chair/Chair.fbx", "Chair/Meshes/Chair.fbx"},
                               Names{"Models/Export/Chair.fbx", "Meshes/Chair.fbx"}, Names{"LOD0/Chair.fbx", "LOD1/Chair.fbx"},
                               Names{"Pack/Chair/FBX/model.fbx", "Pack/Chair/glTF/scene.gltf"}}) {
        INFO(names.front());
        const FabChoice choice = chooseFabModel(plynth::test::fabContentsOf(names), FabOptions{});
        CHECK(choice.status.ok());
        CHECK(choice.otherStems.empty());
    }
}
