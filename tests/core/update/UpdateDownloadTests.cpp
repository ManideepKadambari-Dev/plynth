#include "UpdateTestSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

using namespace plynth;
using namespace plynth::test::update;
using namespace plynth::update_detail;

namespace {

// The entries of a folder, none when it is missing.
std::vector<fs::path> entries(const fs::path& folder)
{
    std::vector<fs::path> found;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) found.push_back(it->path());
    return found;
}

// Reads a whole file with every kind of sharing allowed, as a reader beside the held installer must.
std::string readShared(const fs::path& file)
{
    const HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    std::string bytes;
    char buffer[65536];
    DWORD read = 0;
    while (ReadFile(handle, buffer, sizeof buffer, &read, nullptr) && read > 0) bytes.append(buffer, read);
    CloseHandle(handle);
    return bytes;
}

// Says whether a file can be opened for writing right now.
bool canWrite(const fs::path& file)
{
    const HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    CloseHandle(handle);
    return true;
}

// Checks the offer of a release served for the test and returns it.
UpdateOffer offerFrom(const std::string& version)
{
    const UpdateCheck check = checkForUpdate();
    plynth::test::requireOk(check.status);
    REQUIRE(check.offer);
    REQUIRE(check.offer->version == version);
    return *check.offer;
}

}

TEST_CASE("a verified installer is kept locked under its own name", "[update][download]") {
    plynth::test::TempDir tmp("update-download-good");
    ReleaseServer server;
    const TestKey key = makeKey();
    const ScopedUpdateEnvironment env(testEnvironment(server, {key}, tmp.path()));
    const std::string bytes = fakeInstaller();
    server.setList(releaseList({{"v0.2.0"}}));
    server.publish(key, "0.2.0", bytes);

    long long lastDone = -1, lastTotal = -1;
    UpdateDownload download = downloadUpdate(offerFrom("0.2.0"), [&](long long done, long long total) {
        lastDone = done;
        lastTotal = total;
    });
    plynth::test::requireOk(download.status);
    REQUIRE(download.installer);
    const fs::path file = download.installer->file();
    CHECK(file.filename() == "PlynthSetup.exe");
    CHECK(sameFolder(file.parent_path().parent_path(), tmp.path() / "updates"));
    CHECK(isDownloadFolderName(file.parent_path().filename().wstring()));
    CHECK(download.installer->version() == "0.2.0");
    CHECK(download.installer->size() == static_cast<long long>(bytes.size()));
    CHECK(lastDone == static_cast<long long>(bytes.size()));
    CHECK(lastTotal == static_cast<long long>(bytes.size()));
    CHECK(entries(file.parent_path()).size() == 1);
    CHECK(readShared(file) == bytes);

    // No other handle may write, rename, replace or delete the file while it is held.
    CHECK_FALSE(canWrite(file));
    const fs::path moved = file.wstring() + L".moved";
    CHECK_FALSE(MoveFileExW(file.c_str(), moved.c_str(), 0));
    const fs::path other = tmp.path() / "other.exe";
    writeBytes(other, "not the installer");
    CHECK_FALSE(MoveFileExW(other.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING));
    CHECK_FALSE(DeleteFileW(file.c_str()));
    CHECK(readShared(file) == bytes);

    // A moved installer keeps the lock.
    VerifiedInstaller taken = std::move(*download.installer);
    download.installer.reset();
    CHECK_FALSE(canWrite(file));
    { const VerifiedInstaller dropped = std::move(taken); }
    CHECK(canWrite(file));
    CHECK(server.count("/v0.2.0/PlynthSetup.exe") == 1);
}

TEST_CASE("a changed installer is refused and removed", "[update][download]") {
    plynth::test::TempDir tmp("update-download-changed");
    ReleaseServer server;
    const TestKey key = makeKey();
    const ScopedUpdateEnvironment env(testEnvironment(server, {key}, tmp.path()));
    const std::string bytes = fakeInstaller();
    server.setList(releaseList({{"v0.2.0"}}));
    server.publish(key, "0.2.0", bytes);
    std::string changed = bytes;
    changed[changed.size() / 2] = static_cast<char>(changed[changed.size() / 2] ^ 0x01);
    server.setFile("v0.2.0", "PlynthSetup.exe", changed);

    const UpdateDownload download = downloadUpdate(offerFrom("0.2.0"));
    CHECK(download.status.code == ErrorCode::SignatureInvalid);
    CHECK_FALSE(download.installer);
    CHECK(entries(tmp.path() / "updates").empty());
}

TEST_CASE("an installer whose own version differs from the signed one is refused", "[update][download]") {
    plynth::test::TempDir tmp("update-download-resource");
    ReleaseServer server;
    const TestKey key = makeKey();
    const ScopedUpdateEnvironment env(testEnvironment(server, {key}, tmp.path()));
    const std::string bytes = fakeInstaller();
    server.setList(releaseList({{"v0.3.0"}}));
    server.publish(key, "0.3.0", bytes);

    const UpdateDownload download = downloadUpdate(offerFrom("0.3.0"));
    CHECK(download.status.code == ErrorCode::SignatureInvalid);
    CHECK(download.status.message.find("0.3.0") != std::string::npos);
    CHECK(entries(tmp.path() / "updates").empty());
}

TEST_CASE("an interrupted, too big or cancelled download leaves nothing", "[update][download]") {
    plynth::test::TempDir tmp("update-download-interrupted");
    ReleaseServer server;
    const TestKey key = makeKey();
    UpdateEnvironment limits = testEnvironment(server, {key}, tmp.path());
    const std::string bytes = fakeInstaller();
    server.setList(releaseList({{"v0.2.0"}}));
    server.publish(key, "0.2.0", bytes);
    const fs::path updates = tmp.path() / "updates";

    SECTION("interrupted") {
        const ScopedUpdateEnvironment env(limits);
        const UpdateOffer offer = offerFrom("0.2.0");
        server.setCustom([size = bytes.size()](const httplib::Request& request, httplib::Response& response) {
            if (request.path.find("PlynthSetup.exe") == std::string::npos || request.path.find(".minisig") != std::string::npos) {
                return false;
            }
            response.set_content_provider(size, "application/octet-stream", [](std::size_t, std::size_t, httplib::DataSink& sink) {
                const std::string piece(1000, 'x');
                sink.write(piece.data(), piece.size());
                return false;
            });
            return true;
        });
        const UpdateDownload download = downloadUpdate(offer);
        CHECK(download.status.code == ErrorCode::NetworkFailed);
        CHECK(entries(updates).empty());
    }
    SECTION("too big") {
        limits.maxInstallerBytes = 100;
        const ScopedUpdateEnvironment env(limits);
        const UpdateDownload download = downloadUpdate(offerFrom("0.2.0"));
        CHECK(download.status.code == ErrorCode::NetworkFailed);
        CHECK(entries(updates).empty());
    }
    SECTION("full disk") {
        limits.freeSpace = [](const fs::path&) { return std::optional<long long>(1000); };
        const ScopedUpdateEnvironment env(limits);
        const UpdateDownload download = downloadUpdate(offerFrom("0.2.0"));
        CHECK(download.status.code == ErrorCode::CannotWrite);
        CHECK(entries(updates).empty());
    }
    SECTION("cancelled") {
        const ScopedUpdateEnvironment env(limits);
        std::stop_source stop;
        const UpdateDownload download = downloadUpdate(offerFrom("0.2.0"), [&](long long done, long long) {
            if (done > 0) stop.request_stop();
        }, stop.get_token());
        CHECK(download.status.code == ErrorCode::Cancelled);
        CHECK(entries(updates).empty());
    }
}

TEST_CASE("an offer is checked again before anything is downloaded", "[update][download]") {
    plynth::test::TempDir tmp("update-download-offer");
    ReleaseServer server;
    const TestKey key = makeKey();
    const TestKey stranger = makeKey();
    const ScopedUpdateEnvironment env(testEnvironment(server, {key}, tmp.path()));
    const std::string bytes = fakeInstaller();
    server.publish(key, "0.2.0", bytes);

    UpdateOffer offer{"0.2.0", "", sign(key, bytes, commentFor("0.2.0")), true};
    SECTION("a changed comment") {
        SignatureLines lines = signLines(key, bytes, commentFor("0.2.0"));
        lines.trusted = commentFor("0.4.0");
        offer.signature = lines.text();
        CHECK(downloadUpdate(offer).status.code == ErrorCode::SignatureInvalid);
    }
    SECTION("a key that is not trusted") {
        offer.signature = sign(stranger, bytes, commentFor("0.2.0"));
        CHECK(downloadUpdate(offer).status.code == ErrorCode::SignatureInvalid);
    }
    SECTION("a version that differs from the signed one") {
        offer.version = "0.3.0";
        CHECK(downloadUpdate(offer).status.code == ErrorCode::SignatureInvalid);
    }
    SECTION("a version that is not newer") {
        offer = UpdateOffer{"0.1.0", "", sign(key, bytes, commentFor("0.1.0")), true};
        CHECK(downloadUpdate(offer).status.code == ErrorCode::UpdateNotAvailable);
    }
    SECTION("a version below the highest seen") {
        plynth::test::requireOk(rememberSigned(tmp.path() / "update-state.json", v("0.3.0")));
        CHECK(downloadUpdate(offer).status.code == ErrorCode::UpdateNotAvailable);
    }
    CHECK(server.seen().empty());
    CHECK_FALSE(fs::exists(tmp.path() / "updates"));
}

TEST_CASE("an updates folder that is a link is refused", "[update][download]") {
    plynth::test::TempDir tmp("update-download-link");
    ReleaseServer server;
    const TestKey key = makeKey();
    const fs::path target = tmp.path() / "elsewhere";
    const fs::path link = tmp.path() / "updates";
    fs::create_directories(target);
    const std::wstring command = L"mklink /J \"" + link.wstring() + L"\" \"" + target.wstring() + L"\" >nul 2>nul";
    REQUIRE(_wsystem(command.c_str()) == 0);
    REQUIRE((GetFileAttributesW(link.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0);

    {
        const ScopedUpdateEnvironment env(testEnvironment(server, {key}, tmp.path()));
        const std::string bytes = fakeInstaller();
        server.setList(releaseList({{"v0.2.0"}}));
        server.publish(key, "0.2.0", bytes);
        const UpdateDownload download = downloadUpdate(offerFrom("0.2.0"));
        CHECK(download.status.code == ErrorCode::CannotWrite);
        CHECK(server.count("/v0.2.0/PlynthSetup.exe") == 0);
    }
    CHECK(entries(target).empty());
    REQUIRE(RemoveDirectoryW(link.c_str()));
}

TEST_CASE("a file that is a link is not opened as an installer", "[update][download]") {
    plynth::test::TempDir tmp("update-download-openlocked");
    const fs::path file = tmp.path() / "plain.exe";
    writeBytes(file, "bytes");
    void* handle = openLocked(file);
    REQUIRE(handle);
    CHECK_FALSE(canWrite(file));
    CloseHandle(static_cast<HANDLE>(handle));
    CHECK(canWrite(file));
    CHECK_FALSE(openLocked(tmp.path()));
    CHECK_FALSE(openLocked(tmp.path() / "missing.exe"));
    const fs::path hardLink = tmp.path() / "second-name.exe";
    REQUIRE(CreateHardLinkW(hardLink.c_str(), file.c_str(), nullptr));
    CHECK_FALSE(openLocked(file));
}
