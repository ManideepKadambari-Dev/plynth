#pragma once

// httplib brings winsock2.h, which must come before windows.h.
#include <httplib.h>

#include "../../../core/src/update/UpdateInternals.h"
#include "../TestSupport.h"

#include <plynth/core/Update.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <sodium.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Keys, signatures and a release server for the update tests. Every key is made in memory for the test that
// uses it. The server listens on 127.0.0.1 only, so no test reaches the internet.
namespace plynth::test::update {

namespace fs = std::filesystem;
using update_detail::Version;

// A minisign key pair made for one test.
struct TestKey {
    std::array<unsigned char, 8> keyId{};
    std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> publicKey{};
    std::array<unsigned char, crypto_sign_SECRETKEYBYTES> secretKey{};
};

// Writes bytes as standard base64.
inline std::string base64(const unsigned char* bytes, std::size_t size)
{
    std::string out(sodium_base64_encoded_len(size, sodium_base64_VARIANT_ORIGINAL), '\0');
    sodium_bin2base64(out.data(), out.size(), bytes, size, sodium_base64_VARIANT_ORIGINAL);
    out.resize(out.size() - 1);
    return out;
}

// Makes a fresh key pair with a random key id.
inline TestKey makeKey()
{
    REQUIRE(sodium_init() >= 0);
    TestKey key;
    randombytes_buf(key.keyId.data(), key.keyId.size());
    crypto_sign_keypair(key.publicKey.data(), key.secretKey.data());
    return key;
}

// The public key line, as in the second line of a minisign .pub file.
inline std::string publicKeyLine(const TestKey& key)
{
    std::vector<unsigned char> bytes = {'E', 'd'};
    bytes.insert(bytes.end(), key.keyId.begin(), key.keyId.end());
    bytes.insert(bytes.end(), key.publicKey.begin(), key.publicKey.end());
    return base64(bytes.data(), bytes.size());
}

// The key as the updater holds it.
inline update_detail::PublicKey publicKey(const TestKey& key)
{
    const auto parsed = update_detail::parsePublicKey(publicKeyLine(key));
    REQUIRE(parsed);
    return *parsed;
}

// The parts of a .minisig file, so a test can change one of them.
struct SignatureLines {
    std::string untrusted = "untrusted comment: signature from a key made for this test";
    std::string signature;
    std::string trusted;
    std::string commentSignature;

    std::string text() const
    {
        return untrusted + "\n" + signature + "\ntrusted comment: " + trusted + "\n" + commentSignature + "\n";
    }
};

// Signs bytes as "minisign -S -t <comment>" does: the prehashed form over BLAKE2b-512. legacy signs the bytes
// themselves under the old "Ed" mark instead.
inline SignatureLines signLines(const TestKey& key, const std::string& bytes, const std::string& comment, bool legacy = false)
{
    std::array<unsigned char, crypto_sign_BYTES> fileSignature{};
    if (legacy) {
        crypto_sign_detached(fileSignature.data(), nullptr, reinterpret_cast<const unsigned char*>(bytes.data()),
                             bytes.size(), key.secretKey.data());
    } else {
        std::array<unsigned char, crypto_generichash_BYTES_MAX> hash{};
        crypto_generichash(hash.data(), hash.size(), reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(),
                           nullptr, 0);
        crypto_sign_detached(fileSignature.data(), nullptr, hash.data(), hash.size(), key.secretKey.data());
    }
    std::vector<unsigned char> blob = {'E', static_cast<unsigned char>(legacy ? 'd' : 'D')};
    blob.insert(blob.end(), key.keyId.begin(), key.keyId.end());
    blob.insert(blob.end(), fileSignature.begin(), fileSignature.end());

    std::vector<unsigned char> signedComment(fileSignature.begin(), fileSignature.end());
    signedComment.insert(signedComment.end(), comment.begin(), comment.end());
    std::array<unsigned char, crypto_sign_BYTES> commentSignature{};
    crypto_sign_detached(commentSignature.data(), nullptr, signedComment.data(), signedComment.size(), key.secretKey.data());

    SignatureLines lines;
    lines.signature = base64(blob.data(), blob.size());
    lines.trusted = comment;
    lines.commentSignature = base64(commentSignature.data(), commentSignature.size());
    return lines;
}

// The whole .minisig text.
inline std::string sign(const TestKey& key, const std::string& bytes, const std::string& comment, bool legacy = false)
{
    return signLines(key, bytes, comment, legacy).text();
}

// A version from text, which must be valid.
inline Version v(const std::string& text)
{
    const auto parsed = update_detail::parseVersion(text);
    REQUIRE(parsed);
    return *parsed;
}

// The trusted comment for a version written as text.
inline std::string commentFor(const std::string& version)
{
    return update_detail::trustedCommentFor(v(version));
}

// Reads a whole file.
inline std::string readBytes(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The bytes of the fake installer, whose version resource says 0.2.0.
inline std::string fakeInstaller()
{
    const std::string bytes = readBytes(fs::path(PLYNTH_FAKE_INSTALLER));
    REQUIRE_FALSE(bytes.empty());
    return bytes;
}

// A release list as GitHub gives it: one entry per tag, with its draft and pre-release marks.
struct ListedRelease {
    std::string tag;
    bool draft = false;
    bool prerelease = false;
};
inline std::string releaseList(const std::vector<ListedRelease>& releases)
{
    nlohmann::json list = nlohmann::json::array();
    for (const ListedRelease& r : releases) {
        list.push_back({{"tag_name", r.tag}, {"draft", r.draft}, {"prerelease", r.prerelease},
                        {"name", "Release " + r.tag}, {"body", "Text that must never be read."}});
    }
    return list.dump();
}

// A release server on 127.0.0.1. The list is at /api/repos/<owner>/plynth/releases and release files at
// /dl/<owner>/plynth/releases/download/<tag>/<file>. custom, when set, answers first.
class ReleaseServer {
public:
    using Custom = std::function<bool(const httplib::Request&, httplib::Response&)>;
    struct Seen {
        std::string path;
        httplib::Headers headers;
    };

    ReleaseServer()
    {
        server_.Get(".*", [this](const httplib::Request& request, httplib::Response& response) {
            answer(request, response);
        });
        port_ = server_.bind_to_any_port("127.0.0.1");
        REQUIRE(port_ > 0);
        thread_ = std::thread([this] { server_.listen_after_bind(); });
        server_.wait_until_ready();
    }
    ~ReleaseServer()
    {
        server_.stop();
        if (thread_.joinable()) thread_.join();
    }
    ReleaseServer(const ReleaseServer&) = delete;
    ReleaseServer& operator=(const ReleaseServer&) = delete;

    // The address of the server.
    std::string base() const { return "http://127.0.0.1:" + std::to_string(port_); }
    int port() const { return port_; }

    // Sets the release list.
    void setList(std::string json)
    {
        std::lock_guard guard(lock_);
        list_ = std::move(json);
    }
    // Sets a file of a release.
    void setFile(const std::string& tag, const std::string& name, std::string bytes)
    {
        std::lock_guard guard(lock_);
        files_[tag + "/" + name] = std::move(bytes);
    }
    // Publishes a release signed with a key: the installer and its signature under the tag of the version.
    void publish(const TestKey& key, const std::string& version, const std::string& installer)
    {
        setFile("v" + version, "PlynthSetup.exe", installer);
        setFile("v" + version, "PlynthSetup.exe.minisig", sign(key, installer, commentFor(version)));
    }
    // Sets the handler that answers first.
    void setCustom(Custom custom)
    {
        std::lock_guard guard(lock_);
        custom_ = std::move(custom);
    }
    // Every request seen so far.
    std::vector<Seen> seen()
    {
        std::lock_guard guard(lock_);
        return seen_;
    }
    // How many requests asked for paths that end with this text.
    int count(const std::string& ending)
    {
        int n = 0;
        for (const Seen& s : seen()) {
            if (s.path.size() >= ending.size() && s.path.compare(s.path.size() - ending.size(), ending.size(), ending) == 0) ++n;
        }
        return n;
    }

private:
    // Answers one request from the list, the files or the custom handler.
    void answer(const httplib::Request& request, httplib::Response& response)
    {
        Custom custom;
        std::string list;
        std::map<std::string, std::string> files;
        {
            std::lock_guard guard(lock_);
            seen_.push_back({request.path, request.headers});
            custom = custom_;
            list = list_;
            files = files_;
        }
        if (custom && custom(request, response)) return;
        const std::string listPath = "/api/repos/ManideepKadambari-Dev/plynth/releases";
        const std::string filePrefix = "/dl/ManideepKadambari-Dev/plynth/releases/download/";
        if (request.path == listPath) {
            response.set_content(list, "application/json");
            return;
        }
        if (request.path.rfind(filePrefix, 0) == 0) {
            const auto it = files.find(request.path.substr(filePrefix.size()));
            if (it != files.end()) {
                response.set_content(it->second, "application/octet-stream");
                return;
            }
        }
        response.status = 404;
    }

    httplib::Server server_;
    std::thread thread_;
    int port_ = 0;
    std::mutex lock_;
    std::string list_ = "[]";
    std::map<std::string, std::string> files_;
    Custom custom_;
    std::vector<Seen> seen_;
};

// An installed copy of the given version in a folder of the test.
inline Installation installedCopy(const fs::path& folder, const std::string& version = "0.1.0")
{
    return Installation{InstallKind::Installed, folder, version};
}

// The test environment: the server, the keys, an installed copy of version 0.1.0, and state and downloads in
// the test's folder. No blockers, and a stall limit of two seconds.
inline update_detail::UpdateEnvironment testEnvironment(const ReleaseServer& server, const std::vector<TestKey>& keys,
                                                        const fs::path& folder, const std::string& version = "0.1.0")
{
    update_detail::UpdateEnvironment env;
    env.apiBase = server.base() + "/api";
    env.downloadBase = server.base() + "/dl";
    env.allowedHosts = {"127.0.0.1"};
    env.allowPlainHttp = true;
    env.useSystemProxy = false;
    env.stallLimit = std::chrono::milliseconds(2000);
    env.keys.clear();
    for (const TestKey& key : keys) env.keys.push_back(publicKey(key));
    const Installation installed = installedCopy(folder / "install", version);
    env.installation = [installed] { return installed; };
    env.stateFile = folder / "update-state.json";
    env.downloadRoot = folder / "updates";
    env.blockers = [](const Installation&, const fs::path&) { return std::vector<UpdateBlocker>{}; };
    return env;
}

// Writes text to a file, making its folder.
inline void writeBytes(const fs::path& file, const std::string& bytes)
{
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << bytes;
}

}
