// Plays the part of "plynth update" against a release served on 127.0.0.1, for the end to end update test on
// a throwaway GitHub runner. The key is made in memory for this run and never written anywhere. It uses core's
// test environment, which no shipped program can reach.

#include <httplib.h>

#include "UpdateInternals.h"

#include <nlohmann/json.hpp>
#include <plynth/core/Update.h>
#include <sodium.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using plynth::update_detail::Version;

struct Options {
    std::string testCase;
    std::filesystem::path app;
    std::filesystem::path installer;
    std::string current, newer, lower;
};

struct TestKey {
    std::array<unsigned char, 8> keyId{};
    std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> publicKey{};
    std::array<unsigned char, crypto_sign_SECRETKEYBYTES> secretKey{};
};

// Turns UTF-8 text from the command line into a path.
std::filesystem::path utf8Path(const std::string& text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

// Reads the command line. Empty when something is missing.
std::optional<Options> readOptions(int argc, char** argv)
{
    std::map<std::string, std::string> values;
    for (int i = 1; i + 1 < argc; i += 2) values[argv[i]] = argv[i + 1];
    Options options;
    options.testCase = values["--case"];
    options.app = utf8Path(values["--app"]);
    options.installer = utf8Path(values["--installer"]);
    options.current = values["--current"];
    options.newer = values["--newer"];
    options.lower = values["--lower"];
    if (options.testCase.empty() || options.app.empty() || options.installer.empty() || options.current.empty() ||
        options.newer.empty() || options.lower.empty()) {
        return std::nullopt;
    }
    return options;
}

// Reads a whole file into memory.
std::string readFile(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Writes bytes as standard base64.
std::string base64(const unsigned char* bytes, std::size_t size)
{
    std::string out(sodium_base64_encoded_len(size, sodium_base64_VARIANT_ORIGINAL), '\0');
    sodium_bin2base64(out.data(), out.size(), bytes, size, sodium_base64_VARIANT_ORIGINAL);
    out.resize(out.size() - 1);
    return out;
}

// Makes a fresh key pair with a random key id.
TestKey makeKey()
{
    TestKey key;
    randombytes_buf(key.keyId.data(), key.keyId.size());
    crypto_sign_keypair(key.publicKey.data(), key.secretKey.data());
    return key;
}

// The public key line, as in the second line of a minisign .pub file.
std::string publicKeyLine(const TestKey& key)
{
    std::vector<unsigned char> bytes = {'E', 'd'};
    bytes.insert(bytes.end(), key.keyId.begin(), key.keyId.end());
    bytes.insert(bytes.end(), key.publicKey.begin(), key.publicKey.end());
    return base64(bytes.data(), bytes.size());
}

// Signs bytes the way "minisign -S" does with a trusted comment: the prehashed form over BLAKE2b-512.
std::string minisignSignature(const TestKey& key, const std::string& bytes, const std::string& comment)
{
    std::array<unsigned char, crypto_generichash_BYTES_MAX> hash{};
    crypto_generichash(hash.data(), hash.size(), reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(),
                       nullptr, 0);
    std::array<unsigned char, crypto_sign_BYTES> fileSignature{};
    crypto_sign_detached(fileSignature.data(), nullptr, hash.data(), hash.size(), key.secretKey.data());

    std::vector<unsigned char> blob = {'E', 'D'};
    blob.insert(blob.end(), key.keyId.begin(), key.keyId.end());
    blob.insert(blob.end(), fileSignature.begin(), fileSignature.end());

    std::vector<unsigned char> signedComment(fileSignature.begin(), fileSignature.end());
    signedComment.insert(signedComment.end(), comment.begin(), comment.end());
    std::array<unsigned char, crypto_sign_BYTES> commentSignature{};
    crypto_sign_detached(commentSignature.data(), nullptr, signedComment.data(), signedComment.size(),
                         key.secretKey.data());

    return "untrusted comment: signature from a key made for this test\n" + base64(blob.data(), blob.size()) +
           "\ntrusted comment: " + comment + "\n" + base64(commentSignature.data(), commentSignature.size()) + "\n";
}

// Says whether another program can write, delete or rename a file right now. A verified installer must
// refuse all three.
std::map<std::string, bool> changeAttempts(const std::filesystem::path& file)
{
    std::map<std::string, bool> worked;
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    worked["write"] = handle != INVALID_HANDLE_VALUE;
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    const std::filesystem::path moved = file.wstring() + L".moved";
    worked["rename"] = MoveFileExW(file.c_str(), moved.c_str(), 0) != 0;
    if (worked["rename"]) MoveFileExW(moved.c_str(), file.c_str(), 0);
    worked["delete"] = DeleteFileW(file.c_str()) != 0;
    return worked;
}

// Prints the answer as one JSON line and returns the exit code: 0 when the case behaved as it must.
int finish(nlohmann::json answer, bool passed)
{
    answer["passed"] = passed;
    std::cout << answer.dump() << std::endl;
    return passed ? 0 : 1;
}

}

// Serves one fake release, runs the check, the download and for the good case the start of the installer.
int main(int argc, char** argv)
{
    const std::optional<Options> options = readOptions(argc, argv);
    if (!options) {
        std::cerr << "usage: update_driver --case good|changed-installer|changed-comment|downgrade|replaced-file "
                     "--app <installed folder> --installer <newer PlynthSetup.exe> --current <version> "
                     "--newer <version> --lower <version>\n";
        return 2;
    }
    if (sodium_init() < 0) {
        std::cerr << "libsodium could not start\n";
        return 2;
    }
    const std::optional<Version> newer = plynth::update_detail::parseVersion(options->newer);
    const std::optional<Version> lower = plynth::update_detail::parseVersion(options->lower);
    if (!newer || !lower) {
        std::cerr << "the versions must look like 1.2.3\n";
        return 2;
    }

    const std::string& testCase = options->testCase;
    const std::string installerBytes = readFile(options->installer);
    const TestKey key = makeKey();

    // What the fake release page offers in each case.
    Version tag = *newer;
    Version signedVersion = *newer;
    std::string served = installerBytes;
    if (testCase == "changed-comment") {
        signedVersion.patch += 1;
    } else if (testCase == "downgrade") {
        tag = *lower;
        signedVersion = *lower;
    } else if (testCase == "changed-installer") {
        served[served.size() / 2] = static_cast<char>(served[served.size() / 2] ^ 0x01);
    } else if (testCase != "good" && testCase != "replaced-file") {
        std::cerr << "unknown case " << testCase << "\n";
        return 2;
    }
    const std::string tagText = "v" + plynth::update_detail::versionText(tag);
    const std::string signature =
        minisignSignature(key, installerBytes, plynth::update_detail::trustedCommentFor(signedVersion));
    const std::string list = nlohmann::json::array({{{"tag_name", tagText}, {"draft", false}, {"prerelease", true}}}).dump();

    httplib::Server server;
    std::map<std::string, int> requests;
    server.Get(R"(.*/releases/download/([^/]+)/([^/]+))", [&](const httplib::Request& request, httplib::Response& response) {
        const std::string requestedTag = request.matches[1];
        const std::string file = request.matches[2];
        requests[file] += 1;
        if (requestedTag != tagText) {
            response.status = 404;
        } else if (file == plynth::update_detail::kSignatureName) {
            response.set_content(signature, "application/octet-stream");
        } else if (file == plynth::update_detail::kInstallerName) {
            response.set_content(served, "application/octet-stream");
        } else {
            response.status = 404;
        }
    });
    server.Get(R"(.*/releases)", [&](const httplib::Request&, httplib::Response& response) {
        requests["list"] += 1;
        response.set_content(list, "application/json");
    });
    const int port = server.bind_to_any_port("127.0.0.1");
    if (port <= 0) {
        std::cerr << "the local server could not start\n";
        return 2;
    }
    std::thread serving([&] { server.listen_after_bind(); });

    plynth::update_detail::UpdateEnvironment env;
    env.apiBase = "http://127.0.0.1:" + std::to_string(port);
    env.downloadBase = env.apiBase;
    env.allowedHosts = {"127.0.0.1"};
    env.allowPlainHttp = true;
    env.useSystemProxy = false;
    env.keys.clear();
    if (const auto parsed = plynth::update_detail::parsePublicKey(publicKeyLine(key))) env.keys.push_back(*parsed);
    const plynth::Installation installation{plynth::InstallKind::Installed, options->app, options->current};
    env.installation = [installation] { return installation; };

    nlohmann::json answer = {{"case", testCase}, {"started", false}};
    bool passed = false;
    {
        const plynth::update_detail::ScopedUpdateEnvironment scoped(env);
        const plynth::UpdateCheck check = plynth::checkForUpdate();
        answer["check"] = std::string(plynth::errorCodeName(check.status.code));
        answer["check_message"] = check.status.message;
        answer["offer"] = check.offer ? nlohmann::json(check.offer->version) : nlohmann::json(nullptr);
        nlohmann::json warnings = nlohmann::json::array();
        for (const plynth::UpdateWarning& warning : check.warnings) warnings.push_back({{"code", warning.code}, {"message", warning.message}});
        answer["warnings"] = warnings;

        if (testCase == "downgrade" || testCase == "changed-comment") {
            passed = check.status.ok() && !check.offer;
        } else if (check.status.ok() && check.offer) {
            plynth::UpdateDownload download = plynth::downloadUpdate(*check.offer);
            answer["download"] = std::string(plynth::errorCodeName(download.status.code));
            answer["download_message"] = download.status.message;
            if (testCase == "changed-installer") {
                passed = !download.status.ok() && !download.installer;
            } else if (download.status.ok() && download.installer) {
                if (testCase == "replaced-file") {
                    const auto worked = changeAttempts(download.installer->file());
                    answer["changed"] = worked;
                    passed = !worked.at("write") && !worked.at("rename") && !worked.at("delete");
                } else {
                    const plynth::Status started = plynth::launchUpdate(std::move(*download.installer), {}, false);
                    answer["launch"] = std::string(plynth::errorCodeName(started.code));
                    answer["launch_message"] = started.message;
                    answer["started"] = started.ok();
                    passed = started.ok();
                }
            }
        }
    }
    answer["requests"] = requests;
    server.stop();
    serving.join();
    return finish(answer, passed);
}
