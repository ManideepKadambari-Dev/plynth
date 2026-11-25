#include "FakeBlender.h"
#include "MaterialLibrary.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using plynth::cli::testing::errorCode;
using plynth::cli::testing::installFakeBlender;
using plynth::cli::testing::MaterialSettingsReset;
using plynth::cli::testing::PrivateUser;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::ScopedEnvironment;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;

namespace {

const char* const kAddress = "asset_base_id:0c1d4a2e-1111-2222-3333-444455556666";
const char* const kIsMaterial = "This BlenderKit item is a material. Import it in the Materials window or with plynth materials import.";
const char* const kIsModel = "This BlenderKit item is a model. Import it in the Assets window or with plynth assets import.";

// Says whether a text holds one of the made-up secrets.
bool leaks(const std::string& text) {
    return text.find("KEY-MARKER-7f31") != std::string::npos || text.find("TOKEN-MARKER-92ac") != std::string::npos;
}

// A library in temp, with private user folders and an empty scripts stand in.
struct Setup {
    explicit Setup(const std::string& name)
        : temp(name), user(temp), scripts("PLYNTH_BLENDER_SCRIPTS", utf8(temp.path() / "scripts")) {
        std::filesystem::create_directories(temp.path() / "scripts");
        std::ofstream(temp.path() / "scripts" / "plynth_worker.py") << "# stand in for the worker\n";
        std::filesystem::create_directories(temp.path() / "scripts" / "guard" / "startup");
        std::ofstream(temp.path() / "scripts" / "guard" / "startup" / "plynth_fetch_guard.py") << "# stand in for the guard\n";
        library = temp.path() / "Library";
        REQUIRE(runChecked({"library", "init", utf8(library), "--use"}).exitCode == plynth::cli::kExitOk);
        fake = installFakeBlender(temp.path() / "fb", "5.2.0");
    }

    MaterialSettingsReset reset;
    TempFolder temp;
    PrivateUser user;
    ScopedEnvironment scripts;
    std::filesystem::path library;
    std::filesystem::path fake;
};

}

TEST_CASE("CLI answers and progress hold none of what a fetch prints", "[cli][store_key]") {
    for (const char* catalog : {"assets", "materials"}) {
        for (const char* mode : {"success", "failed", "crash", "bad_result", "child", "forged"}) {
            CAPTURE(catalog, mode);
            Setup setup("sk");
            const auto folder = setup.fake.parent_path();
            std::filesystem::rename(setup.fake, folder / "fake_inner.exe");
            std::filesystem::copy_file(PLYNTH_LEAKY_BLENDER, setup.fake);
            std::ofstream(folder / "leaky_mode.txt") << mode;

            const auto outcome = runChecked({catalog, "import", kAddress, "--blender", utf8(setup.fake)});
            CHECK_FALSE(leaks(outcome.text));
            CHECK_FALSE(leaks(outcome.progress));
            for (const auto& file : std::filesystem::recursive_directory_iterator(setup.library)) {
                if (file.is_regular_file()) {
                    INFO(utf8(file.path()));
                    CHECK_FALSE(leaks(readFile(file.path())));
                }
            }
        }
    }
}

TEST_CASE("the wrong kind from the worker is blender_failed", "[cli][store_import][wrong_kind]") {
    {
        Setup setup("sa");
        const ScopedEnvironment item("FAKE_BLENDERKIT", "material");
        const auto outcome = runChecked({"assets", "import", kAddress, "--blender", utf8(setup.fake)});
        CHECK(errorCode(outcome) == "blender_failed");
        CHECK(outcome.text.find(kIsMaterial) != std::string::npos);
    }
    {
        Setup setup("sm");
        const ScopedEnvironment item("FAKE_BLENDERKIT", "ok");
        const auto outcome = runChecked({"materials", "import", kAddress, "--blender", utf8(setup.fake)});
        CHECK(errorCode(outcome) == "blender_failed");
        CHECK(outcome.text.find(kIsModel) != std::string::npos);
    }
}

// The fake's mislabel mode answers ok with the other kind than the job asked for.
TEST_CASE("core still refuses a fetch that reports the other kind as a success", "[cli][store_import][wrong_kind]") {
    const ScopedEnvironment item("FAKE_BLENDERKIT", "mislabel");
    {
        Setup setup("sx");
        const auto outcome = runChecked({"assets", "import", kAddress, "--blender", utf8(setup.fake)});
        REQUIRE(outcome.answer.at("ok") == false);
        CHECK(errorCode(outcome) == "invalid_value");
        CHECK(outcome.text.find(kIsMaterial) != std::string::npos);
    }
    {
        Setup setup("sy");
        const auto outcome = runChecked({"materials", "import", kAddress, "--blender", utf8(setup.fake)});
        REQUIRE(outcome.answer.at("ok") == false);
        CHECK(errorCode(outcome) == "invalid_value");
        CHECK(outcome.text.find(kIsModel) != std::string::npos);
    }
}

TEST_CASE("a bad integrations format is bad_file in the CLI", "[cli][integrations][store_safety]") {
    for (const char* format : {"1", "null", "[]", "{}"}) {
        Setup setup("si");
        std::ofstream(setup.library / "integrations.json")
            << "{\"format\":" << format << ",\"version\":1,\"integrations\":{}}";
        const std::vector<std::vector<std::string>> runs{{"library", "integration", "show", "fab"},
                                                         {"library", "integration", "set", "fab", "--option", "quality=high"},
                                                         {"library", "integration", "reset", "fab"},
                                                         {"assets", "import", kAddress, "--dry-run"}};
        for (const auto& args : runs) {
            CAPTURE(format, args);
            const auto outcome = runChecked(args);
            REQUIRE(outcome.answer.at("ok") == false);
            CHECK(errorCode(outcome) == "bad_file");
        }
    }
}
