#include "GuideSupport.h"

#include <plynth/core/About.h>
#include <plynth/core/Version.h>

#include <catch2/catch_test_macros.hpp>

using namespace plynth;
using plynth::test::TempFolder;
using plynth::test::writeFile;

TEST_CASE("About gives the three licenses and the copyright line", "[about]")
{
    CHECK(kSourceLicense == "GPL-3.0-or-later");
    CHECK(kBinaryLicense == "GPL-3.0-only");
    CHECK(kSampleLicense == "CC0-1.0");
    CHECK(kCopyrightLine == "Copyright 2026 the Plynth authors (see AUTHORS)");
}

TEST_CASE("About works without a licenses folder and says so", "[about]")
{
    TempFolder root;
    const AboutInfo info = aboutInfo(root.path() / "missing");
    CHECK(info.status.code == ErrorCode::NotFound);
    CHECK(info.version == version());
    CHECK(info.sourceLicense == "GPL-3.0-or-later");
    CHECK(info.binaryLicense == "GPL-3.0-only");
    CHECK(info.sampleLicense == "CC0-1.0");
    CHECK(info.copyright == "Copyright 2026 the Plynth authors (see AUTHORS)");
    CHECK(info.notices.empty());
    CHECK(info.licenseFiles.empty());
    CHECK(aboutInfo({}).status.code == ErrorCode::NotFound);
}

TEST_CASE("About reads the notices and lists the license texts sorted", "[about]")
{
    TempFolder root;
    writeFile(root.path() / "THIRD_PARTY_NOTICES.md", "# Notices\n\n| Component | License |\n");
    writeFile(root.path() / "texts" / "zlib.txt", "z");
    writeFile(root.path() / "texts" / "Apache-2.0.txt", "a");
    writeFile(root.path() / "texts" / "MIT.txt", "m");
    std::filesystem::create_directories(root.path() / "texts" / "folder");

    const AboutInfo info = aboutInfo(root.path());
    CHECK(info.status.ok());
    CHECK(info.notices == "# Notices\n\n| Component | License |\n");
    CHECK(info.noticesFile == root.path() / "THIRD_PARTY_NOTICES.md");
    REQUIRE(info.licenseFiles.size() == 3);
    CHECK(info.licenseFiles[0].filename() == "Apache-2.0.txt");
    CHECK(info.licenseFiles[1].filename() == "MIT.txt");
    CHECK(info.licenseFiles[2].filename() == "zlib.txt");
}

TEST_CASE("About without the notices file still lists the texts", "[about]")
{
    TempFolder root;
    writeFile(root.path() / "texts" / "MIT.txt", "m");
    const AboutInfo info = aboutInfo(root.path());
    CHECK(info.status.code == ErrorCode::NotFound);
    CHECK(info.licenseFiles.size() == 1);
}
