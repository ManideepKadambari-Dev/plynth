#include "GuideSupport.h"

#include <plynth/core/LicenseReport.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>

using namespace plynth;
using plynth::test::TempFolder;
using plynth::test::writeFile;

namespace {

const LicenseEntry kCc0{"cc0", "CC0 (public domain)", "https://creativecommons.org/publicdomain/zero/1.0/", true};
const LicenseEntry kCcBy{"cc_by_4", "CC BY 4.0", "https://creativecommons.org/licenses/by/4.0/", true};
const LicenseEntry kPersonal{"personal_use", "Personal use only", "", false};
const LicenseEntry kUnknown{"unknown", "Unknown", "", false};

// One item with a license and a source reference.
LicensedItem item(LicensedKind kind, std::string id, LicenseEntry license, std::string reference = "file.obj")
{
    LicensedItem i;
    i.kind = kind;
    i.id = id;
    i.name = "Name of " + id;
    i.sourceKind = "local_file";
    i.sourceReference = std::move(reference);
    i.importedAt = SessionTime(std::chrono::seconds(1795341600));
    i.license = std::move(license);
    return i;
}

// A mixed list of items with repeats.
std::vector<LicensedItem> mixedItems()
{
    std::vector<LicensedItem> items;
    items.push_back(item(LicensedKind::Material, "oak_planks", kCc0));
    items.push_back(item(LicensedKind::Asset, "sofa", kCc0));
    items.push_back(item(LicensedKind::Asset, "chair", kCcBy));
    items.push_back(item(LicensedKind::Asset, "sofa", kCc0));
    items.push_back(item(LicensedKind::Material, "brick", kPersonal));
    items.push_back(item(LicensedKind::Asset, "lamp", kUnknown));
    LicensedItem gone = item(LicensedKind::Asset, "gone", {}, "");
    gone.inLibrary = false;
    gone.name.clear();
    gone.sourceKind.clear();
    gone.importedAt = {};
    items.push_back(gone);
    return items;
}

}

TEST_CASE("The report sorts by kind and id and keeps each item once", "[licenses]")
{
    const LicenseReport report = makeLicenseReport("Flat", "abc", mixedItems());
    CHECK(report.project == "Flat");
    CHECK(report.planFingerprint == "abc");
    std::vector<std::string> ids;
    for (const LicensedItem& i : report.items) ids.push_back(i.id);
    CHECK(ids == std::vector<std::string>{"chair", "gone", "lamp", "sofa", "brick", "oak_planks"});
    CHECK(report.items[0].kind == LicensedKind::Asset);
    CHECK(report.items[4].kind == LicensedKind::Material);
}

TEST_CASE("An empty report has no items and no warnings", "[licenses]")
{
    const LicenseReport report = makeLicenseReport("Flat", "abc", {});
    CHECK(report.items.empty());
    CHECK(report.warnings.empty());
    const Json json = licenseReportToJson(report);
    CHECK(json["items"].empty());
    CHECK(json["warnings"].empty());
    CHECK(licenseReportText(report).find("None.") != std::string::npos);
}

TEST_CASE("Each warning code is given", "[licenses]")
{
    const LicenseReport report = makeLicenseReport("Flat", "abc", mixedItems());
    std::vector<std::string> codes;
    for (const LicenseWarning& w : report.warnings) codes.push_back(w.code + ":" + w.id);
    CHECK(codes == std::vector<std::string>{"missing_author:chair", "missing_item:gone", "unknown_license:lamp",
                                            "personal_use:brick"});
    for (const LicenseWarning& w : report.warnings) CHECK(!w.message.empty());

    LicensedItem credited = item(LicensedKind::Asset, "chair", kCcBy);
    credited.author = "A. Maker";
    CHECK(makeLicenseReport("Flat", "abc", {credited}).warnings.empty());
    CHECK(makeLicenseReport("Flat", "abc", {item(LicensedKind::Asset, "x", {})}).warnings[0].code == "unknown_license");
}

TEST_CASE("Source references keep only the file name or the web address", "[licenses]")
{
    const LicenseReport report = makeLicenseReport("Flat", "abc", {
        item(LicensedKind::Asset, "a", kCc0, "C:\\Users\\someone\\Downloads\\sofa.obj"),
        item(LicensedKind::Asset, "b", kCc0, "models/chair/chair.obj"),
        item(LicensedKind::Asset, "c", kCc0, "https://www.blenderkit.com/asset/1234"),
        item(LicensedKind::Asset, "d", kCc0, "table.obj"),
        item(LicensedKind::Asset, "e", kCc0, "C:bed.obj"),
    });
    CHECK(report.items[0].sourceReference == "sofa.obj");
    CHECK(report.items[1].sourceReference == "chair.obj");
    CHECK(report.items[2].sourceReference == "https://www.blenderkit.com/asset/1234");
    CHECK(report.items[3].sourceReference == "table.obj");
    CHECK(report.items[4].sourceReference == "bed.obj");
}

TEST_CASE("The report files are the same for shuffled input", "[licenses]")
{
    std::vector<LicensedItem> items = mixedItems();
    const LicenseReport first = makeLicenseReport("Flat", "abc", items);
    std::mt19937 random(7);
    std::shuffle(items.begin(), items.end(), random);
    const LicenseReport second = makeLicenseReport("Flat", "abc", items);
    CHECK(licenseReportToJson(first).dump(2) == licenseReportToJson(second).dump(2));
    CHECK(licenseReportText(first) == licenseReportText(second));
}

TEST_CASE("The report JSON and text hold every field", "[licenses]")
{
    const LicenseReport report = makeLicenseReport("Flat", "abc", mixedItems());
    const Json json = licenseReportToJson(report);
    CHECK(json["format"].get<std::string>() == "plynth-license-report");
    CHECK(json["version"].get<int>() == 1);
    const Json& sofa = json["items"][3];
    CHECK(sofa["kind"].get<std::string>() == "asset");
    CHECK(sofa["id"].get<std::string>() == "sofa");
    CHECK(sofa["author"].is_null());
    CHECK(sofa["source"]["imported_at"].get<std::string>() == "2026-11-22T10:00:00Z");
    CHECK(sofa["license"]["id"].get<std::string>() == "cc0");
    CHECK(sofa["license"]["redistributable"].get<bool>());
    CHECK(!json["items"][1]["in_library"].get<bool>());
    CHECK(json["items"][1]["source"]["imported_at"].is_null());
    CHECK(plynth::test::schemaProblem("license-report.schema.json", nlohmann::json::parse(json.dump())).empty());

    const std::string text = licenseReportText(report);
    CHECK(text.find("Name of sofa (sofa)") != std::string::npos);
    CHECK(text.find("not legal advice") != std::string::npos);
    CHECK(text.ends_with("not legal advice.\n"));
}

TEST_CASE("Used items round trip and a bad file is refused", "[licenses]")
{
    TempFolder root;
    const auto file = root.path() / "build" / "used-items.json";
    CHECK(readUsedItems(file).status.code == ErrorCode::NotFound);

    const UsedItems items{"abc", {"sofa", "chair", "sofa"}, {"oak_planks"}};
    REQUIRE(writeUsedItems(file, items).ok());
    const UsedItemsRead read = readUsedItems(file);
    REQUIRE(read.status.ok());
    CHECK(read.items == UsedItems{"abc", {"chair", "sofa"}, {"oak_planks"}});
    CHECK(plynth::test::schemaProblem("used-items.schema.json",
                                      nlohmann::json::parse(plynth::test::readFile(file))).empty());

    writeFile(file, "not json");
    CHECK(readUsedItems(file).status.code == ErrorCode::BadFile);
    writeFile(file, R"({"format": "plynth-tour", "version": 1, "plan_fingerprint": "", "assets": [], "materials": []})");
    CHECK(readUsedItems(file).status.code == ErrorCode::BadFile);
    writeFile(file, R"({"format": "plynth-used-items", "version": 2, "plan_fingerprint": "", "assets": [], "materials": []})");
    CHECK(readUsedItems(file).status.code == ErrorCode::UnsupportedVersion);
    writeFile(file, R"({"format": "plynth-used-items", "version": 1, "plan_fingerprint": "", "assets": [3], "materials": []})");
    CHECK(readUsedItems(file).status.code == ErrorCode::BadFile);
    writeFile(file, R"({"format": "plynth-used-items", "version": 1, "plan_fingerprint": "", "assets": []})");
    CHECK(readUsedItems(file).status.code == ErrorCode::BadFile);
}
