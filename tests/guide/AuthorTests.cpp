#include "GuideSupport.h"

#include <plynth/core/catalog/AssetInfo.h>
#include <plynth/core/catalog/MaterialInfo.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace plynth;
using plynth::test::readFile;
using plynth::test::TempFolder;
using plynth::test::writeFile;

namespace {

const std::filesystem::path kData = PLYNTH_CORE_TEST_DATA_DIR;

// The fixture text with an author field added after the license.
std::string withAuthor(const std::string& text, const std::string& authorJson)
{
    const std::size_t at = text.find("\"license\"");
    REQUIRE(at != std::string::npos);
    return text.substr(0, at) + "\"author\": " + authorJson + ", " + text.substr(at);
}

}

TEST_CASE("Asset info without an author still reads", "[licenses][author]")
{
    const AssetInfoRead read = readAssetInfo(kData / "asset-info-valid.json");
    REQUIRE(read.status.ok());
    CHECK(read.info.author.empty());
}

TEST_CASE("The asset author round trips and is checked", "[licenses][author]")
{
    TempFolder root;
    const std::filesystem::path file = root.path() / "info.json";
    AssetInfo info = readAssetInfo(kData / "asset-info-valid.json").info;
    info.author = "J\xC3\xBCrgen Maker";
    REQUIRE(writeAssetInfo(file, info).ok());
    const AssetInfoRead back = readAssetInfo(file);
    REQUIRE(back.status.ok());
    CHECK(back.info == info);
    CHECK(plynth::test::schemaProblem("asset-info.schema.json", nlohmann::json::parse(readFile(file))).empty());

    info.author.clear();
    REQUIRE(writeAssetInfo(file, info).ok());
    CHECK(readFile(file).find("\"author\"") == std::string::npos);

    const std::string text = readFile(kData / "asset-info-valid.json");
    writeFile(file, withAuthor(text, "null"));
    CHECK(readAssetInfo(file).status.ok());
    writeFile(file, withAuthor(text, "\"" + std::string(121, 'a') + "\""));
    const AssetInfoRead tooLong = readAssetInfo(file);
    CHECK(tooLong.status.code == ErrorCode::BadFile);
    CHECK(tooLong.status.where == "/author");
    writeFile(file, withAuthor(text, "\"" + std::string(120, 'a') + "\""));
    CHECK(readAssetInfo(file).status.ok());
    writeFile(file, withAuthor(text, "\"two\\nlines\""));
    CHECK(readAssetInfo(file).status.where == "/author");
}

TEST_CASE("The material author round trips and is checked", "[licenses][author]")
{
    TempFolder root;
    const std::filesystem::path file = root.path() / "info.json";
    const MaterialInfoRead old = readMaterialInfo(kData / "materials" / "info-valid.json");
    REQUIRE(old.status.ok());
    CHECK(old.info.author.empty());
    MaterialInfo info = old.info;
    info.author = "Studio A";
    REQUIRE(writeMaterialInfo(file, info).ok());
    const MaterialInfoRead back = readMaterialInfo(file);
    REQUIRE(back.status.ok());
    CHECK(back.info == info);
    CHECK(plynth::test::schemaProblem("material-info.schema.json", nlohmann::json::parse(readFile(file))).empty());

    const std::string text = readFile(kData / "materials" / "info-valid.json");
    writeFile(file, withAuthor(text, "\"" + std::string(121, 'b') + "\""));
    CHECK(readMaterialInfo(file).status.where == "/author");
}

TEST_CASE("Author rules", "[licenses][author]")
{
    CHECK(checkAuthor("").ok());
    CHECK(checkAuthor("A. Maker").ok());
    CHECK(checkAuthor(std::string(120, 'x')).ok());
    CHECK(checkAuthor(std::string(121, 'x')).code == ErrorCode::InvalidValue);
    CHECK(checkAuthor("tab\there").code == ErrorCode::InvalidValue);
}
