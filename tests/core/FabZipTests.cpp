#include <plynth/core/catalog/FabZip.h>

#include "SchemaSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <initializer_list>

using namespace plynth;

namespace {

std::vector<ZipEntry> entriesOf(std::initializer_list<const char*> names)
{
    std::vector<ZipEntry> entries;
    for (const char* name : names) {
        ZipEntry e;
        e.name = name;
        e.size = 1;
        e.folder = e.name.back() == '/';
        entries.push_back(std::move(e));
    }
    return entries;
}

FabZipContents contentsOf(std::initializer_list<const char*> names)
{
    return classifyFabZip(entriesOf(names));
}

const FabModel* findModel(const FabZipContents& c, const std::string& entry)
{
    for (const auto& m : c.models)
        if (m.entry == entry) return &m;
    return nullptr;
}

const FabTexture* findTexture(const FabZipContents& c, const std::string& entry)
{
    for (const auto& t : c.textures)
        if (t.entry == entry) return &t;
    return nullptr;
}

// The layout of a typical Fab model zip with versions.
FabZipContents variantsZip()
{
    return contentsOf({
        "Chair/FBX/Low/Chair_LOD0.fbx",
        "Chair/FBX/Low/Textures/T_Chair_BaseColor_1K.png",
        "Chair/FBX/Medium/Chair_LOD0.fbx",
        "Chair/FBX/Medium/Chair_LOD1.fbx",
        "Chair/FBX/Medium/Textures/T_Chair_BaseColor_1K.png",
        "Chair/FBX/Medium/Textures/T_Chair_BaseColor_2K.png",
        "Chair/FBX/Medium/Textures/T_Chair_Normal_2K.png",
        "Chair/FBX/High/Chair_LOD0.fbx",
        "Chair/FBX/High/Textures/T_Chair_BaseColor_2K.png",
        "Chair/glTF/Medium/Chair.gltf",
        "Chair/glTF/Medium/Chair.bin",
        "Chair/glTF/Medium/Textures/T_Chair_BaseColor_2K.png",
        "Chair/Previews/front.png",
        "Chair/Chair.json",
    });
}

}

TEST_CASE("fab zip classifies model words, quality, LOD and stems", "[fab]")
{
    const auto c = contentsOf({"Chair_LOD0_High.FBX", "ChairLOD1.fbx", "Medium/Table_Low.obj", "Wooden Stool/model.glb"});
    const auto* a = findModel(c, "Chair_LOD0_High.FBX");
    REQUIRE(a);
    CHECK(a->format == "fbx");
    CHECK(a->quality == FabQuality::High);
    CHECK(a->lod == 0);
    CHECK(a->stem == "Chair");

    const auto* b = findModel(c, "ChairLOD1.fbx");
    REQUIRE(b);
    CHECK_FALSE(b->quality.has_value());
    CHECK(b->lod == 1);
    CHECK(b->stem == "Chair");

    const auto* t = findModel(c, "Medium/Table_Low.obj");
    REQUIRE(t);
    CHECK(t->quality == FabQuality::Low);
    CHECK(t->stem == "Table");

    const auto* s = findModel(c, "Wooden Stool/model.glb");
    REQUIRE(s);
    CHECK(s->format == "gltf");
    CHECK(s->stem == "model");
}

TEST_CASE("fab zip takes the stem from the folder when the file name has only special words", "[fab]")
{
    const auto c = contentsOf({"Armchair/FBX/High.fbx"});
    REQUIRE(c.models.size() == 1);
    CHECK(c.models[0].stem == "Armchair");
    CHECK(c.models[0].quality == FabQuality::High);
}

TEST_CASE("fab zip classifies textures, previews, metadata and others", "[fab]")
{
    const auto c = contentsOf({"Textures/T_Chair_BaseColor_2K.png", "Textures/T_x_05K.png", "T_y_4096.jpg", "T_z.png", "Previews/front.jpg",
                               "chair_thumb.png", "Chair.uasset", "Chair.json", "inner.zip", "Chair.bin", "Textures/", "readme.txt"});
    REQUIRE(findTexture(c, "Textures/T_Chair_BaseColor_2K.png"));
    CHECK(findTexture(c, "Textures/T_Chair_BaseColor_2K.png")->resolution == 2048);
    CHECK(findTexture(c, "Textures/T_x_05K.png")->resolution == 512);
    CHECK(findTexture(c, "T_y_4096.jpg")->resolution == 4096);
    CHECK_FALSE(findTexture(c, "T_z.png")->resolution.has_value());
    CHECK(c.textures.size() == 4);
    CHECK(c.previews == std::vector<std::string>{"Previews/front.jpg", "chair_thumb.png"});
    CHECK(c.unreadableModels == std::vector<std::string>{"Chair.uasset"});
    CHECK(c.metadata == std::vector<std::string>{"Chair.json"});
    CHECK(c.innerZips == std::vector<std::string>{"inner.zip"});
    CHECK(c.other == std::vector<std::string>{"Chair.bin", "readme.txt"});
    CHECK(c.models.empty());
}

TEST_CASE("fab zip sets unsafe and encrypted entries aside", "[fab]")
{
    auto entries = entriesOf({"a.fbx", "../b.fbx", "c.fbx"});
    entries[1].unsafe = true;
    entries[2].encrypted = true;
    const auto c = classifyFabZip(entries);
    REQUIRE(c.models.size() == 1);
    CHECK(c.models[0].entry == "a.fbx");
    CHECK(c.unsafe == std::vector<std::string>{"../b.fbx", "c.fbx"});
}

TEST_CASE("fab zip looks like a model zip or a material zip", "[fab]")
{
    CHECK(looksLikeFabModelZip(variantsZip()));
    CHECK(looksLikeFabModelZip(contentsOf({"a/Chair.fbx", "b/Chair.obj"})));
    CHECK(looksLikeFabModelZip(contentsOf({"Chair_LOD0.fbx"})));
    CHECK(looksLikeFabModelZip(contentsOf({"FBX/Chair.fbx"})));
    CHECK_FALSE(looksLikeFabModelZip(contentsOf({"chair.obj", "chair.mtl"})));
    CHECK_FALSE(looksLikeFabModelZip(contentsOf({})));

    CHECK(looksLikeFabMaterialZip(contentsOf({"Oak_2K.png", "Oak_4K.png"})));
    CHECK(looksLikeFabMaterialZip(contentsOf({"Oak_basecolor.png", "Oak.json"})));
    CHECK_FALSE(looksLikeFabMaterialZip(contentsOf({"Oak_basecolor.png", "Oak_normal.png"})));
}

TEST_CASE("fab choice picks FBX medium by default and follows the options", "[fab]")
{
    const auto c = contentsOf({"FBX/Low/Chair.fbx", "FBX/Medium/Chair.fbx", "FBX/High/Chair.fbx", "glTF/Medium/Chair.gltf"});
    FabOptions options;
    auto choice = chooseFabModel(c, options);
    REQUIRE(choice.status.ok());
    CHECK(choice.model->entry == "FBX/Medium/Chair.fbx");

    options.formats = {"gltf", "fbx"};
    choice = chooseFabModel(c, options);
    CHECK(choice.model->entry == "glTF/Medium/Chair.gltf");

    options = FabOptions{};
    options.quality = FabQuality::High;
    choice = chooseFabModel(c, options);
    CHECK(choice.model->entry == "FBX/High/Chair.fbx");

    // Only Low and Raw exist and medium is wanted: the nearest is Low (after high).
    const auto lowRaw = contentsOf({"Low/Chair.fbx", "Raw/Chair.fbx"});
    options = FabOptions{};
    choice = chooseFabModel(lowRaw, options);
    CHECK(choice.model->entry == "Low/Chair.fbx");

    // Models without a quality word come after the ones with one.
    const auto mixed = contentsOf({"Chair.fbx", "Raw/Chair.fbx"});
    CHECK(chooseFabModel(mixed, options).model->entry == "Raw/Chair.fbx");
}

TEST_CASE("fab choice prefers the lowest LOD, then the shortest path", "[fab]")
{
    const auto c = contentsOf({"Medium/Chair_LOD1.fbx", "Medium/Chair_LOD0.fbx", "Medium/Chair.fbx"});
    CHECK(chooseFabModel(c, FabOptions{}).model->entry == "Medium/Chair_LOD0.fbx");

    const auto d = contentsOf({"deep/er/Chair.fbx", "x/Chair.fbx"});
    CHECK(chooseFabModel(d, FabOptions{}).model->entry == "x/Chair.fbx");
}

TEST_CASE("fab choice takes the textures of the picked version only", "[fab]")
{
    FabOptions options;
    const auto choice = chooseFabModel(variantsZip(), options);
    REQUIRE(choice.status.ok());
    CHECK(choice.model->entry == "Chair/FBX/Medium/Chair_LOD0.fbx");
    CHECK(choice.textures == std::vector<std::string>{"Chair/FBX/Medium/Textures/T_Chair_BaseColor_2K.png",
                                                       "Chair/FBX/Medium/Textures/T_Chair_Normal_2K.png"});
    CHECK(choice.textureResolution == 2048);
    CHECK(choice.metadata == std::optional<std::string>("Chair/Chair.json"));
    CHECK(choice.files == std::vector<std::string>{"Chair/Chair.json", "Chair/FBX/Medium/Chair_LOD0.fbx",
                                                    "Chair/FBX/Medium/Textures/T_Chair_BaseColor_2K.png",
                                                    "Chair/FBX/Medium/Textures/T_Chair_Normal_2K.png"});
    for (const auto& file : choice.files) CHECK(file.find("High") == std::string::npos);
}

TEST_CASE("fab choice finds shared textures above the model and leaves other versions out", "[fab]")
{
    const auto c = contentsOf({"Chair/FBX/Medium/Chair.fbx", "Chair/FBX/High/Chair.fbx", "Chair/FBX/High/Textures/T_Chair_BaseColor.png",
                               "Chair/Textures/T_Chair_BaseColor.png", "Chair/glTF/Textures/T_Chair_Normal.png"});
    const auto choice = chooseFabModel(c, FabOptions{});
    REQUIRE(choice.status.ok());
    CHECK(choice.model->entry == "Chair/FBX/Medium/Chair.fbx");
    CHECK(choice.textures == std::vector<std::string>{"Chair/Textures/T_Chair_BaseColor.png"});
}

TEST_CASE("fab choice picks the texture resolution up to the largest size allowed", "[fab]")
{
    const auto c = contentsOf({"Chair.fbx", "T_Chair_BaseColor_1K.png", "T_Chair_BaseColor_2K.png", "T_Chair_BaseColor_4K.png", "T_Chair_Roughness.png"});
    FabOptions options;
    const auto pick = [&](std::optional<int> max) {
        options.maxTextureSize = max;
        return chooseFabModel(c, options);
    };
    CHECK(pick(4096).textureResolution == 4096);
    CHECK(pick(2048).textureResolution == 2048);
    CHECK(pick(512).textureResolution == 1024);
    CHECK(pick(std::nullopt).textureResolution == 4096);

    const auto choice = pick(2048);
    CHECK(choice.textures == std::vector<std::string>{"T_Chair_BaseColor_2K.png", "T_Chair_Roughness.png"});
    CHECK(choice.skipped.size() == 2);
}

TEST_CASE("fab choice refuses a pack with several models and lists them", "[fab]")
{
    const auto c = contentsOf({"Chair/Chair.fbx", "Table/Table.fbx", "Lamp/Lamp.fbx"});
    const auto choice = chooseFabModel(c, FabOptions{});
    REQUIRE(choice.status.code == ErrorCode::InvalidValue);
    CHECK(choice.status.message == "This zip holds several models: Chair, Lamp, Table. Pick one with the option model.");
    CHECK(choice.otherStems.size() == 3);

    FabOptions options;
    options.model = "Table/Table.fbx";
    const auto picked = chooseFabModel(c, options);
    REQUIRE(picked.status.ok());
    CHECK(picked.model->stem == "Table");
    CHECK(picked.otherStems == std::vector<std::string>{"Chair", "Lamp"});

    options.model = "Nope/Missing.fbx";
    const auto missing = chooseFabModel(c, options);
    CHECK(missing.status.code == ErrorCode::InvalidValue);
    CHECK(missing.status.message == "The zip has no model Nope/Missing.fbx.");
}

TEST_CASE("fab choice shortens a long list of models", "[fab]")
{
    std::vector<ZipEntry> entries;
    for (int i = 0; i < 12; ++i) {
        ZipEntry e;
        e.name = "Item" + std::string(1, static_cast<char>('A' + i)) + "/Item" + std::string(1, static_cast<char>('A' + i)) + ".fbx";
        entries.push_back(e);
    }
    const auto choice = chooseFabModel(classifyFabZip(entries), FabOptions{});
    CHECK(choice.status.message.find("and 2 more") != std::string::npos);
}

TEST_CASE("fab choice refuses zips without a readable model", "[fab]")
{
    const auto uasset = chooseFabModel(contentsOf({"Content/Chair.uasset", "Content/Chair.umap"}), FabOptions{});
    CHECK(uasset.status.code == ErrorCode::InvalidValue);
    CHECK(uasset.status.message ==
          "This zip holds no model Plynth can read (found: .uasset, .umap). Download the FBX or glTF version from Fab.");

    const auto inner = chooseFabModel(contentsOf({"a.zip", "b.zip"}), FabOptions{});
    CHECK(inner.status.message == "This zip holds other zip files. Unzip it and import the zip inside.");

    const auto nothing = chooseFabModel(contentsOf({"readme.txt"}), FabOptions{});
    CHECK(nothing.status.message.find("found: nothing") != std::string::npos);
}

TEST_CASE("fab choice refuses an unsafe texture entry", "[fab]")
{
    auto entries = entriesOf({"Chair.fbx", "../T_Chair_BaseColor.png"});
    entries[1].unsafe = true;
    const auto choice = chooseFabModel(classifyFabZip(entries), FabOptions{});
    CHECK(choice.status.code == ErrorCode::InvalidValue);
    CHECK(choice.status.message == "The zip holds ../T_Chair_BaseColor.png, which cannot be unpacked safely.");
}

TEST_CASE("fab choice takes companion files and metadata", "[fab]")
{
    const auto obj = contentsOf({"chair/chair.obj", "chair/chair.mtl", "chair/other.mtl", "chair/readme.txt"});
    const auto choice = chooseFabModel(obj, FabOptions{});
    REQUIRE(choice.status.ok());
    CHECK(choice.files == std::vector<std::string>{"chair/chair.mtl", "chair/chair.obj", "chair/other.mtl"});

    const auto gltf = contentsOf({"g/Chair.gltf", "g/Chair.bin", "g/Chair.json"});
    const auto g = chooseFabModel(gltf, FabOptions{});
    CHECK(g.files == std::vector<std::string>{"g/Chair.bin", "g/Chair.gltf", "g/Chair.json"});
    CHECK(g.metadata == std::optional<std::string>("g/Chair.json"));
}

TEST_CASE("fab choice adds the textures picked by hand and refuses unknown ones", "[fab]")
{
    const auto c = contentsOf({"Chair.fbx", "Maps/A_2K.png", "Maps/A_4K.png", "Other/Extra_1K.png"});
    FabOptions options;
    options.maxTextureSize = 2048;
    options.maps.push_back(FabMapPick{"*", MapChannel::BaseColor, "Maps/A_4K.png"});
    const auto choice = chooseFabModel(c, options);
    REQUIRE(choice.status.ok());
    CHECK(std::find(choice.textures.begin(), choice.textures.end(), "Maps/A_4K.png") != choice.textures.end());

    options.maps.push_back(FabMapPick{"Wood", MapChannel::Normal, "Nope.png"});
    const auto bad = chooseFabModel(c, options);
    CHECK(bad.status.code == ErrorCode::InvalidValue);
    CHECK(bad.status.message == "The zip has no texture Nope.png.");
}

TEST_CASE("fab material choice filters by resolution and reads the metadata", "[fab]")
{
    const auto c = contentsOf({"Oak/Oak_BaseColor_2K.png", "Oak/Oak_BaseColor_4K.png", "Oak/Oak_Normal_2K.png", "Oak/Oak_Normal_4K.png",
                               "Oak/Preview.png", "Oak/Oak.json"});
    FabOptions options;
    options.maxTextureSize = 2048;
    const auto choice = chooseFabMaterial(c, options);
    REQUIRE(choice.status.ok());
    CHECK_FALSE(choice.model.has_value());
    CHECK(choice.textures == std::vector<std::string>{"Oak/Oak_BaseColor_2K.png", "Oak/Oak_Normal_2K.png"});
    CHECK(choice.metadata == std::optional<std::string>("Oak/Oak.json"));
    CHECK(choice.files.size() == 3);

    const auto none = chooseFabMaterial(contentsOf({"a.txt"}), FabOptions{});
    CHECK(none.status.code == ErrorCode::InvalidValue);
    CHECK(none.status.message == "This zip holds no texture images.");
}

TEST_CASE("fab source json follows the schema", "[fab]")
{
    FabOptions options;
    options.normal = NormalConvention::DirectX;
    options.maps.push_back(FabMapPick{"*", MapChannel::BaseColor, "Chair/FBX/Medium/Textures/T_Chair_BaseColor_2K.png"});
    const auto contents = variantsZip();
    const auto choice = chooseFabModel(contents, options);
    REQUIRE(choice.status.ok());

    const auto json = fabSourceJson(choice, "model", "D:/Downloads/Chair.zip", "D:/Library/assets/.staging/chair/unpacked", options);
    CHECK(test::schemaProblem("fab-source.schema.json", nlohmann::json::parse(json.dump())).empty());
    CHECK(json["model"] == "Chair/FBX/Medium/Chair_LOD0.fbx");
    CHECK(json["model_format"] == "fbx");
    CHECK(json["normal"] == "directx");
    CHECK(json["maps"]["*"]["base_color"] == "Chair/FBX/Medium/Textures/T_Chair_BaseColor_2K.png");

    const auto material = chooseFabMaterial(contentsOf({"Oak_2K.png", "Oak_4K.png"}), FabOptions{});
    const auto mjson = fabSourceJson(material, "material", "D:/Downloads/Oak.zip", "D:/w/unpacked", FabOptions{});
    CHECK(test::schemaProblem("fab-source.schema.json", nlohmann::json::parse(mjson.dump())).empty());
    CHECK(mjson["model"].is_null());
    CHECK(mjson["model_format"].is_null());
}

TEST_CASE("fab choice details list models, picks and skipped entries", "[fab]")
{
    const auto contents = variantsZip();
    const auto choice = chooseFabModel(contents, FabOptions{});
    const auto details = fabChoiceDetails(contents, choice);
    CHECK(details["picked"] == "Chair/FBX/Medium/Chair_LOD0.fbx");
    CHECK(details["models"].size() == 5);
    CHECK(details["models"][0]["quality"] == "high");
    CHECK(details["texture_resolution"] == 2048);
    CHECK(details["previews"].size() == 1);
    CHECK(details["skipped"].size() > 0);
    CHECK(details["other_models"].empty());
}

TEST_CASE("fab options read, refuse and round trip", "[fab]")
{
    FabOptions options;
    REQUIRE(fabOptionsFromJson(Json::parse(R"({"quality":"high","max_texture_size":2048,"formats":["gltf","obj"],"normal":"directx","license":"cc0"})"),
                               false, options).ok());
    CHECK(options.quality == FabQuality::High);
    CHECK(options.maxTextureSize == 2048);
    CHECK(options.formats == std::vector<std::string>{"gltf", "obj"});
    CHECK(options.normal == NormalConvention::DirectX);
    CHECK(options.license == "cc0");

    FabOptions again;
    REQUIRE(fabOptionsFromJson(fabOptionsToJson(options), false, again).ok());
    CHECK(fabOptionsToJson(again) == fabOptionsToJson(options));
    CHECK(fabOptionsToJson(FabOptions{}).dump() ==
          R"({"formats":["fbx","gltf","obj","blend"],"quality":"medium","max_texture_size":4096,"normal":"auto","license":"royalty_free"})");

    FabOptions nullSize;
    REQUIRE(fabOptionsFromJson(Json::parse(R"({"max_texture_size":null})"), false, nullSize).ok());
    CHECK_FALSE(nullSize.maxTextureSize.has_value());

    for (const char* bad : {R"({"model":"a.fbx"})", R"({"maps":{}})", R"({"colour":1})", R"({"formats":[]})", R"({"formats":["fbx","fbx"]})",
                            R"({"formats":["usd"]})", R"({"quality":"ultra"})", R"({"max_texture_size":100})", R"({"max_texture_size":99999})",
                            R"({"normal":"x"})", R"({"license":""})", "[]"}) {
        INFO(bad);
        FabOptions out;
        const auto status = fabOptionsFromJson(Json::parse(bad), false, out);
        CHECK(status.code == ErrorCode::InvalidValue);
        CHECK(out.quality == FabQuality::Medium);
    }
}

TEST_CASE("fab options for one import accept model and maps", "[fab]")
{
    FabOptions options;
    REQUIRE(fabOptionsFromJson(Json::parse(R"({"model":"a/b.fbx","maps":{"*":{"base_color":"T.png"},"Wood":{"normal":"N.png"}}})"), true, options).ok());
    CHECK(options.model == "a/b.fbx");
    REQUIRE(options.maps.size() == 2);
    CHECK(options.maps[0] == FabMapPick{"*", MapChannel::BaseColor, "T.png"});
    CHECK(options.maps[1] == FabMapPick{"Wood", MapChannel::Normal, "N.png"});

    FabOptions bad;
    CHECK(fabOptionsFromJson(Json::parse(R"({"maps":{"*":{"shine":"T.png"}}})"), true, bad).code == ErrorCode::InvalidValue);
    CHECK(fabOptionsToJson(options).contains("model") == false);
}

TEST_CASE("fab flat Megascans zip with every entry at the root", "[fab][zip]")
{
    const auto c = contentsOf({
        "Imagined_Cottage_Equipment_Stool_Plain_Tan_03_abcdefg_High.fbx",
        "Imagined_Cottage_Equipment_Stool_Plain_Tan_03_abcdefg_High_4K_Normal.jpg",
        "Imagined_Cottage_Equipment_Stool_Plain_Tan_03_abcdefg_High_4K_Bump.jpg",
        "Imagined_Cottage_Equipment_Stool_Plain_Tan_03_abcdefg_High_4K_BaseColor.jpg",
        "Imagined_Cottage_Equipment_Stool_Plain_Tan_03_abcdefg_High_4K_AO.jpg",
        "Imagined_Cottage_Equipment_Stool_Plain_Tan_03_abcdefg_High_4K_Displacement.jpg",
        "abcdefg.json",
    });
    REQUIRE(c.models.size() == 1);
    CHECK(c.models[0].quality == FabQuality::High);
    CHECK(c.models[0].stem == "Imagined_Cottage_Equipment_Stool_Plain_Tan_03_abcdefg");
    REQUIRE(c.textures.size() == 5);
    for (const auto& t : c.textures) CHECK(t.resolution == 4096);
    CHECK(c.previews.empty());
    CHECK(looksLikeFabModelZip(c));

    const auto choice = chooseFabModel(c, FabOptions{});
    REQUIRE(choice.status.ok());
    CHECK(choice.model->entry == "Imagined_Cottage_Equipment_Stool_Plain_Tan_03_abcdefg_High.fbx");
    CHECK(choice.textures.size() == 5);
    CHECK_FALSE(choice.textureResolution.has_value());
    CHECK(choice.metadata == std::optional<std::string>("abcdefg.json"));
    CHECK(choice.files.size() == 7);
    CHECK(choice.skipped.empty());
}

TEST_CASE("fab quality names round trip", "[fab]")
{
    for (const char* name : {"low", "medium", "high", "raw"}) CHECK(fabQualityName(*fabQualityFromName(name)) == name);
    CHECK_FALSE(fabQualityFromName("ultra").has_value());
    CHECK(fabQualityName(FabQuality::Raw) == "raw");
}
