#include "FakeBlender.h"
#include "SchemaSupport.h"

#include <plynth/core/catalog/BlenderKit.h>
#include <plynth/core/catalog/StoreFetch.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace plynth;

TEST_CASE("a store fetch never lets a downloaded file run scripts", "[core][fetch][safety]")
{
    plynth::test::IsolatedUser user("fs");
    plynth::test::TempDir tmp("fs");
    BlenderRunOptions base;
    base.scriptsFolder = plynth::test::makeFakeScripts(tmp.path() / "s");
    for (const std::string_view kind : {kFetchKindModel, kFetchKindMaterial}) {
        const auto work = tmp.path() / std::string(kind);
        const auto blender = plynth::test::makeFakeBlender(tmp.path() / ("b-" + std::string(kind)), "5.2.0");
        plynth::test::requireOk(
            runImportFetch(makeBlenderKitFetch("0c1d4a2e-1111-2222-3333-444455556666", kind, BlenderKitOptions{}), blender, base, work).status);
        const std::string args = plynth::test::readTextFile(work / "fake_args.txt");
        INFO(args);
        CHECK(args.find("--background") != std::string::npos);
        CHECK(args.find("--factory-startup") == std::string::npos);
        CHECK(args.find("--enable-autoexec") == std::string::npos);
        const auto off = args.find("--disable-autoexec");
        REQUIRE(off != std::string::npos);
        CHECK(off < args.find(" --python "));
    }
}
