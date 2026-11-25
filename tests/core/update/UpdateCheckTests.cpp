#include "UpdateTestSupport.h"

#include <plynth/core/Settings.h>

#include <catch2/catch_test_macros.hpp>

using namespace plynth;
using namespace plynth::test::update;
using namespace plynth::update_detail;

namespace {

const std::string kInstaller = "installer bytes for the check tests";

// The highest signed version in a state file, as text. Empty when there is none.
std::string highestIn(const fs::path& file)
{
    UpdateStateFile state;
    plynth::test::requireOk(readUpdateState(file, state));
    return state.highestSigned ? versionText(*state.highestSigned) : std::string();
}

}

TEST_CASE("no newer release means no offer", "[update][check]") {
    plynth::test::TempDir tmp("update-check-none");
    ReleaseServer server;
    const TestKey key = makeKey();
    const ScopedUpdateEnvironment env(testEnvironment(server, {key}, tmp.path(), "0.2.0"));
    server.setList(releaseList({{"v0.2.0"}, {"v0.1.0"}}));
    server.publish(key, "0.2.0", kInstaller);
    const UpdateCheck check = checkForUpdate();
    plynth::test::requireOk(check.status);
    CHECK(check.current == "0.2.0");
    CHECK_FALSE(check.offer);
    CHECK(check.warnings.empty());
    CHECK(server.count(".minisig") == 0);
}

TEST_CASE("the highest newer signed release is offered", "[update][check]") {
    plynth::test::TempDir tmp("update-check-newer");
    ReleaseServer server;
    const TestKey key = makeKey();
    const ScopedUpdateEnvironment env(testEnvironment(server, {key}, tmp.path(), "0.1.0"));
    server.setList(releaseList({{"v0.2.0"}, {"v0.3.0"}, {"v0.1.0"}}));
    server.publish(key, "0.2.0", kInstaller);
    server.publish(key, "0.3.0", kInstaller);
    const UpdateCheck check = checkForUpdate();
    plynth::test::requireOk(check.status);
    REQUIRE(check.offer);
    CHECK(check.offer->version == "0.3.0");
    CHECK(check.offer->earlyPreview);
    CHECK(check.offer->releaseUrl == server.base() + "/dl/ManideepKadambari-Dev/plynth/releases/tag/v0.3.0");
    // Ed25519 signatures are deterministic, so the offer carries exactly the published text.
    CHECK(check.offer->signature == sign(key, kInstaller, commentFor("0.3.0")));
    CHECK(server.count("/v0.3.0/PlynthSetup.exe.minisig") == 1);
    CHECK(server.count("/v0.2.0/PlynthSetup.exe.minisig") == 0);
    CHECK(server.count("/PlynthSetup.exe") == 0);
    CHECK(highestIn(tmp.path() / "update-state.json") == "0.3.0");
}

TEST_CASE("0.10 is above 0.9 and 1.0 is not an early preview", "[update][check]") {
    plynth::test::TempDir tmp("update-check-order");
    ReleaseServer server;
    const TestKey key = makeKey();
    const ScopedUpdateEnvironment env(testEnvironment(server, {key}, tmp.path(), "0.9.0"));
    server.setList(releaseList({{"v0.9.0"}, {"v0.10.0"}}));
    server.publish(key, "0.10.0", kInstaller);
    UpdateCheck check = checkForUpdate();
    REQUIRE(check.offer);
    CHECK(check.offer->version == "0.10.0");

    server.setList(releaseList({{"v1.0.0"}, {"v0.10.0"}}));
    server.publish(key, "1.0.0", kInstaller);
    check = checkForUpdate();
    REQUIRE(check.offer);
    CHECK(check.offer->version == "1.0.0");
    CHECK_FALSE(check.offer->earlyPreview);
}

TEST_CASE("the pre-release mark is not used either way", "[update][check]") {
    plynth::test::TempDir tmp("update-check-prerelease");
    ReleaseServer server;
    const TestKey key = makeKey();
    const ScopedUpdateEnvironment env(testEnvironment(server, {key}, tmp.path()));
    server.publish(key, "0.2.0", kInstaller);
    for (const bool mark : {true, false}) {
        server.setList(releaseList({{"v0.2.0", false, mark}}));
        const UpdateCheck check = checkForUpdate();
        REQUIRE(check.offer);
        CHECK(check.offer->version == "0.2.0");
    }
}

TEST_CASE("drafts and odd tags are skipped", "[update][check]") {
    plynth::test::TempDir tmp("update-check-odd");
    ReleaseServer server;
    const TestKey key = makeKey();
    const ScopedUpdateEnvironment env(testEnvironment(server, {key}, tmp.path()));
    // Every odd tag carries a valid signature for 0.4.0, so reading any of them would offer 0.4.0.
    server.publish(key, "0.5.0", kInstaller);
    server.publish(key, "0.3.0", kInstaller);
    for (const char* tag : {"0.4.0", "v0.4.0-rc1", "v0.04.0", "release-0.4.0", "V0.4.0", "v0.4"}) {
        server.setFile(tag, "PlynthSetup.exe.minisig", sign(key, kInstaller, commentFor("0.4.0")));
    }
    server.setList(releaseList({{"v0.5.0", true}, {"0.4.0"}, {"v0.4.0-rc1"}, {"v0.04.0"}, {"release-0.4.0"},
                                {"V0.4.0"}, {"v0.4"}, {"v0.3.0"}}));
    const UpdateCheck check = checkForUpdate();
    plynth::test::requireOk(check.status);
    REQUIRE(check.offer);
    CHECK(check.offer->version == "0.3.0");
    CHECK(server.count("/v0.5.0/PlynthSetup.exe.minisig") == 0);
}

TEST_CASE("a list that is not a release list ends the check", "[update][check]") {
    plynth::test::TempDir tmp("update-check-badlist");
    ReleaseServer server;
    const TestKey key = makeKey();
    const ScopedUpdateEnvironment env(testEnvironment(server, {key}, tmp.path()));
    for (const char* list : {"{}", "not json", "\"text\""}) {
        server.setList(list);
        const UpdateCheck check = checkForUpdate();
        CHECK(check.status.code == ErrorCode::NetworkFailed);
        CHECK_FALSE(check.offer);
    }
    server.setList("[1, \"x\", {\"tag_name\": 7}, {\"draft\": \"no\", \"tag_name\": \"v0.2.0\"}]");
    const UpdateCheck odd = checkForUpdate();
    plynth::test::requireOk(odd.status);
    CHECK_FALSE(odd.offer);
}

TEST_CASE("only the first 30 releases are read", "[update][check]") {
    plynth::test::TempDir tmp("update-check-thirty");
    ReleaseServer server;
    const TestKey key = makeKey();
    const ScopedUpdateEnvironment env(testEnvironment(server, {key}, tmp.path()));
    std::vector<ListedRelease> releases(30, ListedRelease{"v0.0.1"});
    releases.push_back({"v0.9.0"});
    server.setList(releaseList(releases));
    server.publish(key, "0.9.0", kInstaller);
    const UpdateCheck check = checkForUpdate();
    plynth::test::requireOk(check.status);
    CHECK_FALSE(check.offer);
}

TEST_CASE("a missing signature is skipped and a bad one is skipped with a warning", "[update][check]") {
    plynth::test::TempDir tmp("update-check-badsig");
    ReleaseServer server;
    const TestKey key = makeKey();
    const TestKey stranger = makeKey();
    const ScopedUpdateEnvironment env(testEnvironment(server, {key}, tmp.path()));
    server.setList(releaseList({{"v0.5.0"}, {"v0.4.0"}, {"v0.3.0"}, {"v0.2.0"}}));
    server.setFile("v0.5.0", "PlynthSetup.exe", kInstaller);
    server.publish(stranger, "0.4.0", kInstaller);
    SignatureLines replaced = signLines(key, kInstaller, commentFor("0.3.0"));
    replaced.trusted = commentFor("0.9.0");
    server.setFile("v0.3.0", "PlynthSetup.exe.minisig", replaced.text());
    server.publish(key, "0.2.0", kInstaller);

    const UpdateCheck check = checkForUpdate();
    plynth::test::requireOk(check.status);
    REQUIRE(check.offer);
    CHECK(check.offer->version == "0.2.0");
    REQUIRE(check.warnings.size() == 2);
    CHECK(check.warnings[0].code == "signature_invalid");
    CHECK(check.warnings[0].message.find("0.4.0") != std::string::npos);
    CHECK(check.warnings[1].message.find("0.3.0") != std::string::npos);
    CHECK(highestIn(tmp.path() / "update-state.json") == "0.2.0");
}

TEST_CASE("an old installer signed for another version is refused under a new tag", "[update][check]") {
    plynth::test::TempDir tmp("update-check-replay-tag");
    ReleaseServer server;
    const TestKey key = makeKey();
    const ScopedUpdateEnvironment env(testEnvironment(server, {key}, tmp.path()));
    server.setList(releaseList({{"v0.3.0"}}));
    server.setFile("v0.3.0", "PlynthSetup.exe", kInstaller);
    server.setFile("v0.3.0", "PlynthSetup.exe.minisig", sign(key, kInstaller, commentFor("0.2.0")));
    const UpdateCheck check = checkForUpdate();
    plynth::test::requireOk(check.status);
    CHECK_FALSE(check.offer);
    REQUIRE(check.warnings.size() == 1);
    CHECK(highestIn(tmp.path() / "update-state.json").empty());
}

TEST_CASE("the highest signed version seen is remembered and nothing below it is offered", "[update][check]") {
    plynth::test::TempDir tmp("update-check-highest");
    ReleaseServer server;
    const TestKey key = makeKey();
    const ScopedUpdateEnvironment env(testEnvironment(server, {key}, tmp.path()));
    server.publish(key, "0.2.0", kInstaller);
    server.publish(key, "0.3.0", kInstaller);
    server.setList(releaseList({{"v0.3.0"}, {"v0.2.0"}}));
    UpdateCheck check = checkForUpdate();
    REQUIRE(check.offer);
    CHECK(check.offer->version == "0.3.0");
    CHECK(highestIn(tmp.path() / "update-state.json") == "0.3.0");

    // The newer release is hidden: the older signed one is not offered as a replay.
    server.setList(releaseList({{"v0.2.0"}}));
    check = checkForUpdate();
    plynth::test::requireOk(check.status);
    CHECK_FALSE(check.offer);
    CHECK(highestIn(tmp.path() / "update-state.json") == "0.3.0");

    // A copy that saw 0.3.0 before is still offered 0.3.0 when it comes back.
    server.setList(releaseList({{"v0.3.0"}, {"v0.2.0"}}));
    check = checkForUpdate();
    REQUIRE(check.offer);
    CHECK(check.offer->version == "0.3.0");
}

TEST_CASE("a bridge release signed with the old key is offered when the newest uses a new key", "[update][check]") {
    plynth::test::TempDir tmp("update-check-bridge");
    ReleaseServer server;
    const TestKey oldKey = makeKey();
    const TestKey newKey = makeKey();
    const ScopedUpdateEnvironment env(testEnvironment(server, {oldKey}, tmp.path()));
    server.setList(releaseList({{"v0.3.0"}, {"v0.2.0"}}));
    server.publish(newKey, "0.3.0", kInstaller);
    server.publish(oldKey, "0.2.0", kInstaller);
    const UpdateCheck check = checkForUpdate();
    REQUIRE(check.offer);
    CHECK(check.offer->version == "0.2.0");
    CHECK(check.warnings.size() == 1);

    // After the bridge the copy trusts both keys and takes 0.3.0.
    const ScopedUpdateEnvironment both(testEnvironment(server, {oldKey, newKey}, tmp.path(), "0.2.0"));
    const UpdateCheck next = checkForUpdate();
    REQUIRE(next.offer);
    CHECK(next.offer->version == "0.3.0");
}

TEST_CASE("a network failure while reading signatures ends the check with no offer", "[update][check]") {
    plynth::test::TempDir tmp("update-check-network");
    ReleaseServer server;
    const TestKey key = makeKey();
    const ScopedUpdateEnvironment env(testEnvironment(server, {key}, tmp.path()));
    server.setList(releaseList({{"v0.3.0"}, {"v0.2.0"}}));
    server.publish(key, "0.2.0", kInstaller);
    server.setCustom([](const httplib::Request& request, httplib::Response& response) {
        if (request.path.find("/v0.3.0/") == std::string::npos) return false;
        response.status = 500;
        return true;
    });
    const UpdateCheck check = checkForUpdate();
    CHECK(check.status.code == ErrorCode::NetworkFailed);
    CHECK_FALSE(check.offer);
    CHECK(server.count("/v0.2.0/PlynthSetup.exe.minisig") == 0);
}

TEST_CASE("a copy that is not installed makes no request", "[update][check]") {
    plynth::test::TempDir tmp("update-check-not-installed");
    ReleaseServer server;
    const TestKey key = makeKey();
    UpdateEnvironment notInstalled = testEnvironment(server, {key}, tmp.path());
    const Installation copy{InstallKind::NotInstalled, tmp.path() / "build", "0.1.0"};
    notInstalled.installation = [copy] { return copy; };
    const ScopedUpdateEnvironment env(std::move(notInstalled));
    server.setList(releaseList({{"v0.2.0"}}));
    server.publish(key, "0.2.0", kInstaller);
    const long long connections = httpConnectionCount();

    const UpdateCheck check = checkForUpdate();
    CHECK(check.status.code == ErrorCode::UpdateNotInstalled);
    CHECK(check.status.message.find(std::string(kNotInstalledMessage)) == 0);
    CHECK(check.status.message.find(std::string(kReleasesPage)) != std::string::npos);
    CHECK_FALSE(check.offer);

    UpdateOffer offer{"0.2.0", "", sign(key, kInstaller, commentFor("0.2.0")), true};
    const UpdateDownload download = downloadUpdate(offer);
    CHECK(download.status.code == ErrorCode::UpdateNotInstalled);
    CHECK_FALSE(download.installer);

    CHECK(httpConnectionCount() == connections);
    CHECK(server.seen().empty());
    CHECK_FALSE(fs::exists(tmp.path() / "updates"));
    CHECK_FALSE(fs::exists(tmp.path() / "update-state.json"));
}

TEST_CASE("the real test program is not an installed copy and makes no request", "[update][check]") {
    const long long connections = httpConnectionCount();
    CHECK(currentInstallation().kind == InstallKind::NotInstalled);
    const UpdateCheck check = checkForUpdate();
    CHECK(check.status.code == ErrorCode::UpdateNotInstalled);
    CHECK(httpConnectionCount() == connections);
    CHECK(updateStatus().installation.kind == InstallKind::NotInstalled);
    CHECK_FALSE(updateStatus().checkAtStart);
}

TEST_CASE("an installed copy without a built in key cannot update and makes no request", "[update][check]") {
    plynth::test::TempDir tmp("update-check-nokey");
    ReleaseServer server;
    // As the shipped program with an empty trusted-keys.txt.
    UpdateEnvironment noKeys = testEnvironment(server, {}, tmp.path());
    noKeys.keys.clear();
    const ScopedUpdateEnvironment env(std::move(noKeys));
    server.setList(releaseList({{"v0.2.0"}}));
    const long long connections = httpConnectionCount();
    const UpdateCheck check = checkForUpdate();
    CHECK(check.status.code == ErrorCode::UpdateNotAvailable);
    CHECK(httpConnectionCount() == connections);
    CHECK(server.seen().empty());
}

TEST_CASE("the check at start runs only for an installed copy with readable settings and the switch on", "[update][check]") {
    plynth::test::TempDir tmp("update-check-start");
    const Installation installed = installedCopy(tmp.path());
    const Installation build{InstallKind::NotInstalled, tmp.path(), "0.1.0"};
    SettingsLoad on;
    SettingsLoad off;
    off.settings.updateCheck = false;
    SettingsLoad bad;
    bad.status = fail(ErrorCode::BadFile, "bad");
    CHECK(shouldCheckAtStart(installed, on));
    CHECK_FALSE(shouldCheckAtStart(installed, off));
    CHECK_FALSE(shouldCheckAtStart(installed, bad));
    CHECK_FALSE(shouldCheckAtStart(build, on));
    CHECK_FALSE(shouldCheckAtStart(build, off));
    CHECK_FALSE(shouldCheckAtStart(build, bad));
}

TEST_CASE("with the switch off the start makes no request", "[update][check]") {
    plynth::test::TempDir tmp("update-check-off");
    ReleaseServer server;
    const TestKey key = makeKey();
    const ScopedUpdateEnvironment env(testEnvironment(server, {key}, tmp.path()));
    MachineSettings settings;
    settings.updateCheck = false;
    const fs::path file = tmp.path() / "settings.json";
    plynth::test::requireOk(saveSettings(settings, file));
    const long long connections = httpConnectionCount();
    // What the app and the CLI do at start: ask shouldCheckAtStart, and check only when it says yes.
    if (shouldCheckAtStart(environment().installation(), loadSettings(file))) checkForUpdate();
    CHECK(httpConnectionCount() == connections);
    CHECK(server.seen().empty());
}
