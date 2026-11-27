#include "FakeBlender.h"
#include "ZipTestWriter.h"

#include "../../core/src/blender/BlenderDownloadSteps.h"

#include <plynth/core/Blender.h>
#include <plynth/core/BlenderDownload.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/ZipListing.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stop_token>
#include <string>
#include <vector>

using namespace plynth;
namespace fs = std::filesystem;
namespace bd = plynth::blender_download;
using plynth::test::ZipTestEntry;

namespace {

// Reads a whole file as bytes.
std::string fileBytes(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// Reads a saved page or list from the test data.
std::string fixture(const std::string& name)
{
    return fileBytes(fs::path(PLYNTH_TEST_DATA_DIR) / "blender-download" / name);
}

// Writes bytes to a file.
void writeBytes(const fs::path& file, const std::string& bytes)
{
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// The names of everything directly in a folder, sorted. Empty when the folder is not there.
std::vector<std::string> namesIn(const fs::path& folder)
{
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(folder, ec)) names.push_back(pathToUtf8(entry.path().filename()));
    std::sort(names.begin(), names.end());
    return names;
}

// A zip put in a staging folder below root, with an offer that carries the zip's own checksum.
struct StagedZip {
    fs::path staging;
    BlenderOffer offer;
};

// Writes a zip for a version into a new staging folder of root and makes its offer.
StagedZip stageZip(const fs::path& root, const std::vector<ZipTestEntry>& entries, const std::string& version = "5.2.3",
                   const std::string& stagingName = ".download-test")
{
    StagedZip staged;
    staged.staging = root / stagingName;
    fs::create_directories(staged.staging);
    staged.offer.version = *parseBlenderVersion(version);
    const std::string series = std::to_string(staged.offer.version.major) + "." + std::to_string(staged.offer.version.minor);
    staged.offer.zipName = "blender-" + version + "-windows-x64.zip";
    staged.offer.zipUrl = bd::releaseFolderUrl(series) + staged.offer.zipName;
    plynth::test::writeZipFile(staged.staging / staged.offer.zipName, entries);
    const FileHash hash = sha256OfFile(staged.staging / staged.offer.zipName);
    plynth::test::requireOk(hash.status);
    staged.offer.sha256 = hash.sha256;
    return staged;
}

// The entries of a zip shaped like blender.org's, with the fake Blender as blender.exe.
std::vector<ZipTestEntry> fakeBlenderZip(const std::string& version)
{
    const std::string folder = "blender-" + version + "-windows-x64/";
    return {
        {folder + "blender.exe", fileBytes(PLYNTH_FAKE_BLENDER)},
        {folder + "fake_version.txt", version},
        {folder + "readme.html", "<p>readme</p>"},
        {folder + "5.2/scripts/startup/bl_app_template.py", "# script"},
    };
}

// A search of the download folder and the given folders only.
BlenderSearch onlyDownloaded(std::vector<fs::path> folders = {})
{
    BlenderSearch search;
    search.environment = false;
    search.programFolders = false;
    search.registry = false;
    search.store = false;
    search.steam = false;
    search.systemPath = false;
    search.downloaded = true;
    search.extraFolders = std::move(folders);
    return search;
}

// A good offer, for the offer checks.
BlenderOffer goodOffer()
{
    BlenderOffer offer;
    offer.version = {5, 2, 3};
    offer.zipName = "blender-5.2.3-windows-x64.zip";
    offer.zipUrl = "https://download.blender.org/release/Blender5.2/blender-5.2.3-windows-x64.zip";
    offer.sha256 = std::string(64, 'a');
    return offer;
}

}

TEST_CASE("the highest Windows zip of the series is picked from a saved listing page", "[core][blender][download]") {
    const std::string page = fixture("release-folder.html");
    REQUIRE(bd::testedSeries() == "5.2");
    REQUIRE(bd::releaseFolderUrl("5.2") == "https://download.blender.org/release/Blender5.2/");

    // 5.2.10 beats 5.2.9 by number; arm64, msi, msix, torrent, other series and a leading zero are left out.
    REQUIRE(pickBlenderZip(page, "5.2") == std::optional<std::string>("blender-5.2.10-windows-x64.zip"));
    REQUIRE(pickBlenderZip(page, "5.3") == std::optional<std::string>("blender-5.3.0-windows-x64.zip"));
    REQUIRE_FALSE(pickBlenderZip(page, "4.2"));
    REQUIRE_FALSE(pickBlenderZip("", "5.2"));
    REQUIRE_FALSE(pickBlenderZip("<a href=\"blender-5.2.3-windows-arm64.zip\">x</a>", "5.2"));

    REQUIRE(bd::listedSize(page, "blender-5.2.10-windows-x64.zip") == 381234567);
    REQUIRE(bd::listedSize(page, "blender-5.2.2-windows-x64.zip") == 380633088);
    REQUIRE(bd::listedSize(page, "blender-5.2.10.sha256") == 830);
    REQUIRE(bd::listedSize(page, "blender-9.9.9-windows-x64.zip") == 0);

    // A page that gives sizes as "363M" still offers the zip, without a size.
    const std::string apache = fixture("apache-folder.html");
    REQUIRE(pickBlenderZip(apache, "5.2") == std::optional<std::string>("blender-5.2.4-windows-x64.zip"));
    REQUIRE(bd::listedSize(apache, "blender-5.2.4-windows-x64.zip") == 0);

    REQUIRE(bd::zipVersion("blender-5.2.10-windows-x64.zip", "5.2") == BlenderVersion{5, 2, 10});
    REQUIRE(bd::zipVersion("blender-5.2.0-windows-x64.zip", "5.2") == BlenderVersion{5, 2, 0});
    REQUIRE_FALSE(bd::zipVersion("blender-5.2.010-windows-x64.zip", "5.2"));
    REQUIRE_FALSE(bd::zipVersion("blender-5.2.-windows-x64.zip", "5.2"));
    REQUIRE_FALSE(bd::zipVersion("blender-5.2.3-windows-x64.zip.torrent", "5.2"));
    REQUIRE_FALSE(bd::zipVersion("blender-5.20.3-windows-x64.zip", "5.2"));
    REQUIRE_FALSE(bd::zipVersion("Blender-5.2.3-windows-x64.zip", "5.2"));
    REQUIRE(bd::checksumListName("blender-5.2.10-windows-x64.zip") == "blender-5.2.10.sha256");
    REQUIRE(bd::copyFolderName("blender-5.2.10-windows-x64.zip") == "blender-5.2.10-windows-x64");
}

TEST_CASE("checksum lines", "[core][blender][download]") {
    const std::string list = fixture("blender-5.2.10.sha256");
    REQUIRE(sha256For(list, "blender-5.2.10-windows-x64.zip") ==
            std::optional<std::string>("a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90"));
    REQUIRE(sha256For(list, "blender-5.2.10-linux-x64.tar.xz") ==
            std::optional<std::string>("4f6a1c0de2b39a8e7c5d4b3a29180f7e6d5c4b3a2918f7e6d5c4b3a29180f7e6"));
    REQUIRE_FALSE(sha256For(list, "blender-5.2.10-windows-x64.msi"));
    REQUIRE_FALSE(sha256For(list, "blender-5.2.10-windows-x64"));
    REQUIRE_FALSE(sha256For("", "blender-5.2.10-windows-x64.zip"));

    const std::string hash(64, 'b');
    const std::string name = "blender-5.2.3-windows-x64.zip";
    REQUIRE(sha256For(hash + " *" + name + "\r\n", name) == hash);
    REQUIRE(sha256For("\r\n  " + hash + "\t" + name + "  \r\n", name) == hash);
    REQUIRE(sha256For(hash + "  " + name + "\n" + hash + "  " + name + "\n", name) == hash);
    REQUIRE_FALSE(sha256For(hash + "  " + name + "\n" + std::string(64, 'c') + "  " + name + "\n", name));
    REQUIRE_FALSE(sha256For(std::string(63, 'b') + "  " + name, name));
    REQUIRE_FALSE(sha256For(std::string(65, 'b') + "  " + name, name));
    REQUIRE_FALSE(sha256For(std::string(63, 'b') + "g  " + name, name));
    REQUIRE_FALSE(sha256For(hash + "  " + name + ".torrent", name));
    REQUIRE_FALSE(sha256For(hash, name));
}

TEST_CASE("SHA-256 of a known file", "[core][blender][download]") {
    plynth::test::TempDir tmp("blender-download-sha");
    writeBytes(tmp.path() / "empty.bin", "");
    writeBytes(tmp.path() / "abc.bin", "abc");
    writeBytes(tmp.path() / "million.bin", std::string(1000000, 'a'));

    FileHash hash = sha256OfFile(tmp.path() / "empty.bin");
    plynth::test::requireOk(hash.status);
    REQUIRE(hash.sha256 == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(sha256OfFile(tmp.path() / "abc.bin").sha256 == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(sha256OfFile(tmp.path() / "million.bin").sha256 == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    hash = sha256OfFile(tmp.path() / "missing.bin");
    REQUIRE(hash.status.code == ErrorCode::CannotRead);
    REQUIRE(hash.sha256.empty());

    std::stop_source stop;
    stop.request_stop();
    REQUIRE(sha256OfFile(tmp.path() / "abc.bin", stop.get_token()).status.code == ErrorCode::Cancelled);
}

TEST_CASE("only an offer for download.blender.org's own release folder is taken", "[core][blender][download]") {
    plynth::test::requireOk(bd::checkOffer(goodOffer()));
    const auto refused = [](BlenderOffer offer) { return bd::checkOffer(offer).code == ErrorCode::InvalidValue; };
    BlenderOffer offer = goodOffer();
    offer.zipUrl = "http://download.blender.org/release/Blender5.2/blender-5.2.3-windows-x64.zip";
    REQUIRE(refused(offer));
    offer = goodOffer();
    offer.zipUrl = "https://download.blender.org.example.com/release/Blender5.2/blender-5.2.3-windows-x64.zip";
    REQUIRE(refused(offer));
    offer = goodOffer();
    offer.zipName = "../blender-5.2.3-windows-x64.zip";
    offer.zipUrl = "https://download.blender.org/release/Blender5.2/../blender-5.2.3-windows-x64.zip";
    REQUIRE(refused(offer));
    offer = goodOffer();
    offer.version = {5, 2, 4};
    REQUIRE(refused(offer));
    offer = goodOffer();
    offer.sha256 = std::string(64, 'A');
    REQUIRE(refused(offer));
    offer = goodOffer();
    offer.sha256 = std::string(63, 'a');
    REQUIRE(refused(offer));

    REQUIRE(blenderDownloadStageName(BlenderDownloadStage::Downloading) == "downloading");
    REQUIRE(blenderDownloadStageName(BlenderDownloadStage::Checking) == "checking");
    REQUIRE(blenderDownloadStageName(BlenderDownloadStage::Unpacking) == "unpacking");
    REQUIRE(blenderDownloadStageName(BlenderDownloadStage::Inspecting) == "inspecting");
    REQUIRE(blenderSourceName(BlenderSource::Downloaded) == "downloaded");
}

TEST_CASE("the Blender zip's names are checked and blender.exe is found", "[core][blender][download]") {
    plynth::test::TempDir tmp("blender-download-names");
    const auto exeFolderOf = [&](const std::vector<ZipTestEntry>& entries, std::string& folder) {
        plynth::test::writeZipFile(tmp.path() / "a.zip", entries);
        return bd::checkBlenderZipEntries(listZipEntries(tmp.path() / "a.zip"), folder);
    };
    std::string folder = "old";
    plynth::test::requireOk(exeFolderOf({{"blender-5.2.3-windows-x64/readme.html", "x"}, {"blender-5.2.3-windows-x64/blender.exe", "x"}}, folder));
    REQUIRE(folder == "blender-5.2.3-windows-x64");
    plynth::test::requireOk(exeFolderOf({{"blender.exe", "x"}, {"readme.html", "x"}}, folder));
    REQUIRE(folder.empty());
    REQUIRE(exeFolderOf({{"blender-5.2.3-windows-x64/bin/blender.exe", "x"}}, folder).code == ErrorCode::BadFile);
    REQUIRE(exeFolderOf({{"blender-5.2.3-windows-x64/blender.txt", "x"}}, folder).code == ErrorCode::BadFile);
    REQUIRE(bd::checkBlenderZipEntries(ZipListing{}, folder).code == ErrorCode::BadFile);
}

TEST_CASE("unsafe zip names are refused and nothing is left", "[core][blender][download]") {
    plynth::test::TempDir tmp("blender-download-unsafe");
    const fs::path root = tmp.path() / "root";
    const std::string top = "blender-5.2.3-windows-x64/";
    const std::vector<std::string> unsafe = {
        "../evil.txt",          top + "../../evil.txt", top + "..\\..\\evil.txt", "/evil.txt",
        "C:/evil.txt",          top + "c:evil.txt",     top + "nul.txt",          top + "COM1",
        top + "trailing. /evil.txt", top + "evil.txt ", top + "a\x01" "b.txt",
    };
    for (std::size_t i = 0; i < unsafe.size(); ++i) {
        INFO(unsafe[i]);
        const StagedZip staged = stageZip(root, {{top + "blender.exe", "x"}, {unsafe[i], "evil"}});
        const BlenderDownload got = bd::installBlenderZip(root, staged.staging, staged.offer, {}, {});
        REQUIRE(got.status.code == ErrorCode::BadFile);
        REQUIRE_FALSE(got.install);
        REQUIRE(namesIn(root).empty());
        REQUIRE_FALSE(fs::exists(tmp.path() / "evil.txt"));
    }

    const StagedZip locked = stageZip(root, {{top + "blender.exe", "x", true, true}});
    REQUIRE(bd::installBlenderZip(root, locked.staging, locked.offer, {}, {}).status.code == ErrorCode::BadFile);
    REQUIRE(namesIn(root).empty());

    const StagedZip noBlender = stageZip(root, {{top + "readme.html", "x"}});
    REQUIRE(bd::installBlenderZip(root, noBlender.staging, noBlender.offer, {}, {}).status.code == ErrorCode::BadFile);
    REQUIRE(namesIn(root).empty());

    const StagedZip notZip = stageZip(root, {{top + "blender.exe", "x"}});
    writeBytes(notZip.staging / notZip.offer.zipName, "not a zip");
    BlenderOffer offer = notZip.offer;
    offer.sha256 = sha256OfFile(notZip.staging / offer.zipName).sha256;
    REQUIRE(bd::installBlenderZip(root, notZip.staging, offer, {}, {}).status.code == ErrorCode::BadFile);
    REQUIRE(namesIn(root).empty());
}

TEST_CASE("a download that does not match its checksum is deleted", "[core][blender][download]") {
    plynth::test::TempDir tmp("blender-download-checksum");
    const fs::path root = tmp.path() / "root";
    StagedZip staged = stageZip(root, {{"blender-5.2.3-windows-x64/blender.exe", "x"}});
    staged.offer.sha256 = std::string(64, '0');
    const BlenderDownload got = bd::installBlenderZip(root, staged.staging, staged.offer, {}, {});
    REQUIRE(got.status.code == ErrorCode::ChecksumFailed);
    REQUIRE_FALSE(got.install);
    REQUIRE(namesIn(root).empty());

    // An offer that was not built for download.blender.org is refused before anything is read.
    StagedZip other = stageZip(root, {{"blender-5.2.3-windows-x64/blender.exe", "x"}});
    other.offer.zipUrl = "https://example.com/blender-5.2.3-windows-x64.zip";
    REQUIRE(bd::installBlenderZip(root, other.staging, other.offer, {}, {}).status.code == ErrorCode::InvalidValue);
    REQUIRE(namesIn(root).empty());
}

TEST_CASE("a stopped unpack leaves nothing", "[core][blender][download]") {
    plynth::test::TempDir tmp("blender-download-stop");
    const fs::path root = tmp.path() / "root";
    for (const BlenderDownloadStage stopAt :
         {BlenderDownloadStage::Checking, BlenderDownloadStage::Unpacking, BlenderDownloadStage::Inspecting}) {
        INFO(blenderDownloadStageName(stopAt));
        const StagedZip staged = stageZip(root, fakeBlenderZip("5.2.3"));
        std::stop_source stop;
        std::vector<BlenderDownloadStage> seen;
        const BlenderDownload got = bd::installBlenderZip(root, staged.staging, staged.offer,
                                                          [&](const BlenderDownloadProgress& p) {
                                                              seen.push_back(p.stage);
                                                              if (p.stage == stopAt) stop.request_stop();
                                                          },
                                                          stop.get_token());
        REQUIRE(got.status.code == ErrorCode::Cancelled);
        REQUIRE_FALSE(got.install);
        REQUIRE(std::find(seen.begin(), seen.end(), stopAt) != seen.end());
        REQUIRE(namesIn(root).empty());
    }

    const StagedZip staged = stageZip(root, fakeBlenderZip("5.2.3"));
    std::stop_source before;
    before.request_stop();
    REQUIRE(bd::installBlenderZip(root, staged.staging, staged.offer, {}, before.get_token()).status.code == ErrorCode::Cancelled);
    REQUIRE(namesIn(root).empty());
}

TEST_CASE("a downloaded copy is unpacked, found and ranked", "[core][blender][download]") {
    plynth::test::TempDir tmp("blender-download-found");
    plynth::test::ScopedEnv local("LOCALAPPDATA", pathToUtf8(tmp.path() / "local"));
    const fs::path root = downloadedBlenderRoot();
    REQUIRE(root == tmp.path() / "local" / "Plynth" / "Blender");

    const StagedZip staged = stageZip(root, fakeBlenderZip("5.2.3"));
    std::vector<BlenderDownloadStage> stages;
    const BlenderDownload got = bd::installBlenderZip(root, staged.staging, staged.offer,
                                                      [&](const BlenderDownloadProgress& p) {
                                                          if (stages.empty() || stages.back() != p.stage) stages.push_back(p.stage);
                                                      },
                                                      {});
    plynth::test::requireOk(got.status);
    REQUIRE(got.install);
    REQUIRE(got.install->version == BlenderVersion{5, 2, 3});
    REQUIRE(got.install->usable());
    REQUIRE(got.install->sources == std::vector<BlenderSource>{BlenderSource::Downloaded});
    REQUIRE(stages == std::vector<BlenderDownloadStage>{BlenderDownloadStage::Checking, BlenderDownloadStage::Unpacking,
                                                        BlenderDownloadStage::Inspecting});
    const fs::path copy = root / "blender-5.2.3-windows-x64";
    REQUIRE(got.install->executable == copy / "blender.exe");
    REQUIRE(fs::is_regular_file(copy / "readme.html"));
    REQUIRE(fs::is_regular_file(copy / "5.2" / "scripts" / "startup" / "bl_app_template.py"));
    REQUIRE(namesIn(root) == std::vector<std::string>{"blender-5.2.3-windows-x64"});
    const nlohmann::json marker = nlohmann::json::parse(fileBytes(copy / ".plynth-download"));
    REQUIRE(marker["zip"] == "blender-5.2.3-windows-x64.zip");
    REQUIRE(marker["sha256"] == staged.offer.sha256);
    REQUIRE(marker["version"] == "5.2.3");

    // The same version again keeps the copy that is there.
    const StagedZip again = stageZip(root, fakeBlenderZip("5.2.3"), "5.2.3", ".download-again");
    plynth::test::requireOk(bd::installBlenderZip(root, again.staging, again.offer, {}, {}).status);
    REQUIRE(namesIn(root) == std::vector<std::string>{"blender-5.2.3-windows-x64"});

    // A folder without the marker and a staging folder are not downloads.
    plynth::test::makeFakeBlender(root / "blender-5.2.1-windows-x64", "5.2.1");
    plynth::test::makeFakeBlender(root / ".download-left", "5.2.2");
    const std::vector<BlenderCandidate> candidates = findBlenderCandidates(onlyDownloaded());
    REQUIRE(candidates.size() == 1);
    REQUIRE(candidates[0].executable == fs::weakly_canonical(copy / "blender.exe"));
    REQUIRE(candidates[0].sources == std::vector<BlenderSource>{BlenderSource::Downloaded});
    BlenderSearch without = onlyDownloaded();
    without.downloaded = false;
    REQUIRE(findBlenderCandidates(without).empty());

    // An installed copy of the same version comes first, though its path sorts after.
    plynth::test::makeFakeBlender(tmp.path() / "zz-same" / "Blender 5.2", "5.2.3");
    std::vector<BlenderInstall> installs = findBlenderInstalls(onlyDownloaded({tmp.path() / "zz-same"}));
    REQUIRE(installs.size() == 2);
    REQUIRE(installs[0].sources == std::vector<BlenderSource>{BlenderSource::Extra});
    REQUIRE(installs[1].sources == std::vector<BlenderSource>{BlenderSource::Downloaded});
    REQUIRE(preferredBlender(installs, std::nullopt) == 0);

    // A newer downloaded copy ranks like any newer Blender, and a chosen one stays chosen.
    const fs::path older = plynth::test::makeFakeBlender(tmp.path() / "zz-old" / "Blender 4.5", "4.5.0");
    installs = findBlenderInstalls(onlyDownloaded({tmp.path() / "zz-old"}));
    REQUIRE(installs.size() == 2);
    REQUIRE(installs[0].sources == std::vector<BlenderSource>{BlenderSource::Downloaded});
    REQUIRE(installs[1].version == BlenderVersion{4, 5, 0});
    REQUIRE(preferredBlender(installs, std::nullopt) == 0);
    REQUIRE(preferredBlender(installs, older) == 1);

    // The chosen path of a downloaded copy lists both sources.
    BlenderSearch chosen = onlyDownloaded();
    chosen.chosen = copy / "blender.exe";
    const std::vector<BlenderCandidate> both = findBlenderCandidates(chosen);
    REQUIRE(both.size() == 1);
    REQUIRE(both[0].sources == std::vector<BlenderSource>{BlenderSource::Settings, BlenderSource::Downloaded});
}
