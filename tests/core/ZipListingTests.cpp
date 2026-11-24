#include <plynth/core/ZipListing.h>

#include "TestSupport.h"
#include "ZipTestWriter.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>

using namespace plynth;
using namespace plynth::test;

namespace {

std::filesystem::path zipData(const std::string& name)
{
    return std::filesystem::path(PLYNTH_TEST_DATA_DIR) / "zip" / name;
}

std::vector<std::string> expectedNames(const std::string& zipName)
{
    std::ifstream in(zipData(zipName).replace_extension(".txt"), std::ios::binary);
    std::vector<std::string> names;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) names.push_back(line);
    }
    return names;
}

std::vector<std::string> namesOf(const ZipListing& listing)
{
    std::vector<std::string> names;
    for (const auto& e : listing.entries) names.push_back(e.name);
    return names;
}

}

TEST_CASE("zip listing gives the names Python reads from the committed zips", "[zip]")
{
    for (const char* name : {"deflated.zip", "utf8.zip", "cp437.zip", "zip64.zip", "encrypted.zip"}) {
        INFO(name);
        const auto listing = listZipEntries(zipData(name));
        REQUIRE(listing.status.ok());
        REQUIRE(namesOf(listing) == expectedNames(name));
    }
}

TEST_CASE("zip listing reads sizes, folders and flags", "[zip]")
{
    const auto listing = listZipEntries(zipData("deflated.zip"));
    REQUIRE(listing.status.ok());
    REQUIRE(listing.entries.size() == 3);
    CHECK(listing.entries[0].folder);
    CHECK(listing.entries[1].size == 9 * 40);
    CHECK(listing.entries[1].compressedSize < listing.entries[1].size);
    CHECK_FALSE(listing.entries[1].folder);
    CHECK_FALSE(listing.entries[1].unsafe);
    CHECK_FALSE(listing.entries[1].encrypted);

    const auto enc = listZipEntries(zipData("encrypted.zip"));
    REQUIRE(enc.entries.size() == 2);
    CHECK(enc.entries[0].encrypted);
    CHECK_FALSE(enc.entries[1].encrypted);
}

TEST_CASE("zip listing reads a ZIP64 entry", "[zip]")
{
    const auto listing = listZipEntries(zipData("zip64.zip"));
    REQUIRE(listing.status.ok());
    REQUIRE(listing.entries.size() == 1);
    CHECK(listing.entries[0].name == "big/Model.fbx");
    CHECK(listing.entries[0].size == 16);
    CHECK(listing.entries[0].compressedSize == 16);
}

TEST_CASE("zip listing decodes code page 437 and UTF-8 names", "[zip]")
{
    const auto cp = listZipEntries(zipData("cp437.zip"));
    REQUIRE(cp.status.ok());
    CHECK(cp.entries[0].name == "caf\xC3\xA9.txt");

    const auto u8 = listZipEntries(zipData("utf8.zip"));
    REQUIRE(u8.status.ok());
    CHECK(u8.entries[0].name == "caf\xC3\xA9/st\xC3\xBChl.obj");
}

TEST_CASE("zip listing flags unsafe names", "[zip]")
{
    TempDir dir("zip-unsafe");
    const auto path = dir.path() / "unsafe.zip";
    writeZipFile(path, zipEntriesNamed({"ok/a.txt", "../evil.txt", "a/../../b.txt", "/abs.txt", "C:x.txt", "dots..name.txt", "back\\..\\c.txt"}));
    const auto listing = listZipEntries(path);
    REQUIRE(listing.status.ok());
    REQUIRE(listing.entries.size() == 7);
    CHECK_FALSE(listing.entries[0].unsafe);
    CHECK(listing.entries[1].unsafe);
    CHECK(listing.entries[2].unsafe);
    CHECK(listing.entries[3].unsafe);
    CHECK(listing.entries[4].unsafe);
    CHECK_FALSE(listing.entries[5].unsafe);
    CHECK(listing.entries[6].unsafe);
    CHECK(listing.entries[6].name == "back/../c.txt");
}

TEST_CASE("zip listing refuses files that are not zips or are damaged", "[zip]")
{
    const auto missing = listZipEntries(zipData("no_such_file.zip"));
    CHECK(missing.status.code == ErrorCode::NotFound);

    const auto text = listZipEntries(zipData("notzip.txt.bin"));
    CHECK(text.status.code == ErrorCode::BadFile);
    CHECK(text.status.message == "This is not a zip file.");

    const auto cut = listZipEntries(zipData("cutoff.zip"));
    CHECK(cut.status.code == ErrorCode::BadFile);
    CHECK(cut.status.message == "The zip file is damaged.");

    TempDir dir("zip-small");
    const auto tiny = dir.path() / "tiny.zip";
    std::ofstream(tiny, std::ios::binary) << "PK";
    CHECK(listZipEntries(tiny).status.message == "This is not a zip file.");
}

TEST_CASE("zip listing refuses a directory that claims too many entries", "[zip]")
{
    TempDir dir("zip-many");
    const auto path = dir.path() / "many.zip";
    writeZipFile(path, zipEntriesNamed({"a.txt"}), kMaxZipEntries + 1);
    const auto listing = listZipEntries(path);
    CHECK(listing.status.code == ErrorCode::BadFile);
    CHECK(listing.entries.empty());

    writeZipFile(path, zipEntriesNamed({"a.txt"}), 5);
    CHECK(listZipEntries(path).status.message == "The zip file is damaged.");
}

TEST_CASE("zip listing reads a zip written by the test writer, in directory order", "[zip]")
{
    TempDir dir("zip-writer");
    const auto path = dir.path() / "w.zip";
    writeZipFile(path, zipEntriesNamed({"b/", "b/z.txt", "a.txt"}));
    const auto listing = listZipEntries(path);
    REQUIRE(listing.status.ok());
    CHECK(namesOf(listing) == std::vector<std::string>{"b/", "b/z.txt", "a.txt"});
    CHECK(listing.entries[0].folder);
}
