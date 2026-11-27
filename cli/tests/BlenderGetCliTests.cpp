#include "FakeBlender.h"
#include "SampleProject.h"
#include "SettingsGuard.h"
#include "TestSupport.h"

#include "../src/BlenderGet.h"
#include "../../core/src/blender/BlenderDownloadSteps.h"
#include "../../tests/core/ZipTestWriter.h"

#include <plynth/cli/Words.h>
#include <plynth/core/Blender.h>
#include <plynth/core/BlenderDownload.h>
#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace bd = plynth::blender_download;
using plynth::BlenderDownload;
using plynth::BlenderDownloadProgress;
using plynth::BlenderDownloadStage;
using plynth::BlenderOffer;
using plynth::BlenderOfferCheck;
using plynth::ErrorCode;
using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::GuardedSettingsReset;
using plynth::cli::testing::installFakeBlender;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::ScopedEnvironment;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;

namespace {

const std::string kSha256 = "a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90";

// Reads a whole file as bytes.
std::string fileBytes(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// Reads a saved blender.org page or checksum list, the same ones the core tests read.
std::string fixture(const std::string& name) {
    return fileBytes(fs::path(PLYNTH_BLENDER_DOWNLOAD_DATA) / name);
}

// The offer core makes from the saved release folder page and its checksum list.
BlenderOffer savedOffer() {
    const std::string series = bd::testedSeries();
    const std::string page = fixture("release-folder.html");
    const auto zip = plynth::pickBlenderZip(page, series);
    REQUIRE(zip);
    const auto sha = plynth::sha256For(fixture("blender-5.2.10.sha256"), *zip);
    REQUIRE(sha);
    BlenderOffer offer;
    offer.version = *bd::zipVersion(*zip, series);
    offer.zipName = *zip;
    offer.zipUrl = bd::releaseFolderUrl(series) + *zip;
    offer.sha256 = *sha;
    offer.size = bd::listedSize(page, *zip);
    return offer;
}

// The names of everything directly in a folder, sorted. Empty when the folder is not there.
std::vector<std::string> namesIn(const fs::path& folder) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        names.push_back(plynth::pathToUtf8(entry.path().filename()));
    }
    std::sort(names.begin(), names.end());
    return names;
}

// The stages of the progress lines in order, each run of the same stage once.
std::vector<std::string> stagesOf(const std::string& progress) {
    std::vector<std::string> stages;
    std::istringstream lines(progress);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty()) {
            continue;
        }
        const nlohmann::json parsed = nlohmann::json::parse(line);
        const std::string stage = parsed.at("progress").at("stage").get<std::string>();
        CHECK(parsed.at("progress").at("message").is_string());
        if (stages.empty() || stages.back() != stage) {
            stages.push_back(stage);
        }
    }
    return stages;
}

// The Blender saved in the settings, or null.
nlohmann::json savedBlender() {
    const auto shown = runChecked({"settings", "show"});
    REQUIRE(shown.exitCode == kExitOk);
    return shown.answer.at("result").at("settings").at("blender");
}

// Stands in for blender.org. The offer comes from the saved listing, and the download hands over a zip the test
// made, so what follows the network (checksum, unpack, inspect) is core's own code. The download folder is a
// temporary one through LOCALAPPDATA.
struct FakeBlenderOrg {
    // Makes the zip, the offer with its checksum, and puts the fake in place.
    FakeBlenderOrg()
        : temp("blender-get"), local("LOCALAPPDATA", plynth::pathToUtf8(temp.path() / "local")),
          zip(temp.path() / "server" / "blender-5.2.10-windows-x64.zip") {
        fs::create_directories(zip.parent_path());
        const std::string folder = "blender-5.2.10-windows-x64/";
        plynth::test::writeZipFile(zip, {{folder + "blender.exe", fileBytes(PLYNTH_FAKE_BLENDER)},
                                         {folder + "fake_version.txt", "5.2.10"},
                                         {folder + "readme.html", "<p>readme</p>"}});
        offer = savedOffer();
        const plynth::FileHash hash = plynth::sha256OfFile(zip);
        REQUIRE(hash.status.ok());
        offer.sha256 = hash.sha256;

        plynth::cli::BlenderGetSource source;
        source.findOffer = [this](std::stop_token) {
            ++offers;
            BlenderOfferCheck check;
            check.status = offerStatus;
            if (offerStatus.ok()) {
                check.offer = offer;
            }
            return check;
        };
        source.download = [this](const BlenderOffer& asked,
                                 const std::function<void(const BlenderDownloadProgress&)>& onProgress,
                                 std::stop_token stop) {
            ++downloads;
            if (!downloadStatus.ok()) {
                return BlenderDownload{downloadStatus, std::nullopt};
            }
            const auto report = [&](const BlenderDownloadProgress& step) {
                if (stopAt == step.stage) {
                    stopper.request_stop();
                }
                if (onProgress) {
                    onProgress(step);
                }
            };
            const fs::path root = plynth::downloadedBlenderRoot();
            const fs::path staging = root / ".download-cli-test";
            fs::create_directories(staging);
            fs::copy_file(zip, staging / plynth::pathFromUtf8(asked.zipName), fs::copy_options::overwrite_existing);
            const auto size = static_cast<long long>(fs::file_size(zip));
            report({BlenderDownloadStage::Downloading, size, size});
            return bd::installBlenderZip(root, staging, asked, report, stop);
        };
        plynth::cli::setBlenderGetSource(std::move(source));
    }

    // Puts core's own source back.
    ~FakeBlenderOrg() { plynth::cli::setBlenderGetSource({}); }

    // The download folder, inside the temporary LOCALAPPDATA.
    fs::path root() const { return plynth::downloadedBlenderRoot(); }

    GuardedSettingsReset settings;
    TempFolder temp;
    ScopedEnvironment local;
    fs::path zip;
    BlenderOffer offer;
    plynth::Status offerStatus;
    plynth::Status downloadStatus;
    std::optional<BlenderDownloadStage> stopAt;
    std::stop_source stopper;
    int offers = 0;
    int downloads = 0;
};

}

TEST_CASE("blender get is a command word and help lists its options", "[cli][blender_get]") {
    CHECK(plynth::cli::isCommandWord("blender"));
    const auto help = runChecked({"help"});
    bool found = false;
    for (const auto& command : help.answer.at("result").at("commands")) {
        if (command.at("command") != "blender get") {
            continue;
        }
        found = true;
        std::vector<std::string> names;
        for (const auto& option : command.at("options")) {
            names.push_back(option.at("name").get<std::string>());
        }
        for (const char* name : {"--yes", "--if-missing", "--quiet"}) {
            CHECK(std::find(names.begin(), names.end(), name) != names.end());
        }
    }
    CHECK(found);
}

TEST_CASE("blender get without --yes answers with the offer and downloads nothing", "[cli][blender_get]") {
    FakeBlenderOrg fake;
    fake.offer = savedOffer();
    const auto outcome = runChecked({"blender", "get"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    CHECK(result.at("state") == "offer");
    CHECK(result.at("version") == "5.2.10");
    CHECK(result.at("size") == 381234567);
    CHECK(result.at("source") == "https://download.blender.org/release/Blender5.2/blender-5.2.10-windows-x64.zip");
    CHECK(result.at("sha256") == kSha256);
    CHECK(result.at("target") == utf8(fake.root() / "blender-5.2.10-windows-x64"));
    CHECK(result.at("next") == "plynth blender get --yes");
    CHECK(result.at("note").get<std::string>().find("Nothing was downloaded") != std::string::npos);
    CHECK(fake.offers == 1);
    CHECK(fake.downloads == 0);
    CHECK(namesIn(fake.root()).empty());
    CHECK(savedBlender().is_null());
    CHECK(stagesOf(outcome.progress) == std::vector<std::string>{"find"});

    fake.offer.size = 0;
    CHECK(runChecked({"blender", "get", "--quiet"}).answer.at("result").at("size").is_null());
}

TEST_CASE("blender get --yes downloads, checks, unpacks and chooses the new Blender", "[cli][blender_get]") {
    FakeBlenderOrg fake;
    const auto outcome = runChecked({"blender", "get", "--yes"});
    INFO(outcome.text);
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    const fs::path copy = fake.root() / "blender-5.2.10-windows-x64";
    CHECK(result.at("state") == "installed");
    CHECK(result.at("version") == "5.2.10");
    CHECK(result.at("path") == utf8(copy / "blender.exe"));
    CHECK(result.at("folder") == utf8(copy));
    CHECK(result.at("source") == fake.offer.zipUrl);
    CHECK(result.at("sha256") == fake.offer.sha256);
    CHECK(result.at("reused") == false);
    CHECK(result.at("selected") == true);
    CHECK(fs::is_regular_file(copy / "blender.exe"));
    CHECK(fs::is_regular_file(copy / ".plynth-download"));
    CHECK(namesIn(fake.root()) == std::vector<std::string>{"blender-5.2.10-windows-x64"});
    CHECK(savedBlender() == utf8(copy / "blender.exe"));
    CHECK(stagesOf(outcome.progress) ==
          std::vector<std::string>{"find", "downloading", "checking", "unpacking", "inspecting", "select"});

    // The test program sets PLYNTH_BLENDER, which commands would use first, so the answer says so.
    bool warned = false;
    for (const auto& warning : outcome.answer.value("warnings", nlohmann::json::array())) {
        warned = warned || warning.at("code") == "blender_variable_set";
    }
    CHECK(warned);

    // A second run keeps the copy that is there.
    const auto again = runChecked({"blender", "get", "--yes", "--quiet"});
    REQUIRE(again.exitCode == kExitOk);
    CHECK(again.answer.at("result").at("reused") == true);
    CHECK(again.progress.empty());
    CHECK(namesIn(fake.root()) == std::vector<std::string>{"blender-5.2.10-windows-x64"});
}

TEST_CASE("blender get --if-missing does nothing when a usable Blender is found", "[cli][blender_get]") {
    FakeBlenderOrg fake;
    const fs::path own = installFakeBlender(fake.temp.path() / "own", "5.2.0");
    ScopedEnvironment variable("PLYNTH_BLENDER", plynth::pathToUtf8(own));
    for (const std::vector<std::string>& args : {std::vector<std::string>{"blender", "get", "--if-missing"},
                                                 std::vector<std::string>{"blender", "get", "--yes", "--if-missing"}}) {
        const auto outcome = runChecked(args);
        REQUIRE(outcome.exitCode == kExitOk);
        const auto& result = outcome.answer.at("result");
        CHECK(result.at("state") == "skipped");
        CHECK(result.at("blender").at("path") == utf8(own));
        CHECK(result.at("blender").at("version") == "5.2.0");
    }
    CHECK(fake.offers == 0);
    CHECK(fake.downloads == 0);
    CHECK(namesIn(fake.root()).empty());
    CHECK(savedBlender().is_null());
}

TEST_CASE("blender get --if-missing goes on when no usable Blender is found", "[cli][blender_get]") {
    // The test program's PLYNTH_BLENDER names a file that does not exist.
    FakeBlenderOrg fake;
    const auto outcome = runChecked({"blender", "get", "--if-missing"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("state") == "offer");
    CHECK(outcome.answer.at("result").at("next") == "plynth blender get --yes --if-missing");
    CHECK(fake.offers == 1);
    CHECK(fake.downloads == 0);
    CHECK(stagesOf(outcome.progress) == std::vector<std::string>{"look", "find"});
}

TEST_CASE("blender get answers network_failed when blender.org cannot be reached", "[cli][blender_get]") {
    FakeBlenderOrg fake;
    fake.offerStatus = plynth::fail(ErrorCode::NetworkFailed, "download.blender.org could not be reached.");
    for (const std::vector<std::string>& args :
         {std::vector<std::string>{"blender", "get"}, std::vector<std::string>{"blender", "get", "--yes"}}) {
        const auto outcome = runChecked(args);
        CHECK(outcome.exitCode == kExitFailed);
        CHECK(errorCode(outcome) == "network_failed");
    }
    CHECK(fake.downloads == 0);

    fake.offerStatus = {};
    fake.downloadStatus = plynth::fail(ErrorCode::NetworkFailed, "The download stopped.");
    const auto outcome = runChecked({"blender", "get", "--yes"});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "network_failed");
    CHECK(namesIn(fake.root()).empty());
    CHECK(savedBlender().is_null());
}

TEST_CASE("blender get answers checksum_failed and keeps nothing when the zip does not match", "[cli][blender_get]") {
    FakeBlenderOrg fake;
    fake.offer.sha256 = kSha256;
    const auto outcome = runChecked({"blender", "get", "--yes"});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "checksum_failed");
    CHECK(fake.downloads == 1);
    CHECK(namesIn(fake.root()).empty());
    CHECK(savedBlender().is_null());
}

TEST_CASE("blender get answers cancelled and keeps nothing when it is stopped", "[cli][blender_get]") {
    FakeBlenderOrg fake;
    plynth::cli::CliHost host;
    host.stop = fake.stopper.get_token();
    fake.stopAt = BlenderDownloadStage::Unpacking;
    const auto outcome = runChecked({"blender", "get", "--yes"}, fs::current_path(), host);
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "cancelled");
    CHECK(fake.downloads == 1);
    CHECK(namesIn(fake.root()).empty());
    CHECK(savedBlender().is_null());

    // A stop that came first sends nothing.
    const auto early = runChecked({"blender", "get", "--yes"}, fs::current_path(), host);
    CHECK(errorCode(early) == "cancelled");
    CHECK(fake.offers == 1);
    CHECK(fake.downloads == 1);
}
