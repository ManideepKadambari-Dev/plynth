#include "UpdateTestSupport.h"

#include <catch2/catch_test_macros.hpp>

using namespace plynth;
using namespace plynth::test::update;
using namespace plynth::update_detail;

namespace {

const std::string kBytes = "The bytes of an installer made for this test.";

// Decodes a base64 line of a signature, changes one byte and encodes it again.
std::string flipByte(const std::string& line, std::size_t at)
{
    std::vector<unsigned char> raw(line.size());
    std::size_t length = 0;
    REQUIRE(sodium_base642bin(raw.data(), raw.size(), line.data(), line.size(), nullptr, &length, nullptr,
                              sodium_base64_VARIANT_ORIGINAL) == 0);
    raw.resize(length);
    REQUIRE(at < raw.size());
    raw[at] ^= 0x01;
    return base64(raw.data(), raw.size());
}

// Checks a signature text against the keys and returns the status code.
ErrorCode checkCode(const std::string& text, const std::vector<PublicKey>& keys)
{
    CheckedSignature checked;
    return checkSignature(text, keys, checked).code;
}

}

TEST_CASE("a good signature verifies and names its version", "[update][signature]") {
    const TestKey key = makeKey();
    CheckedSignature checked;
    plynth::test::requireOk(checkSignature(sign(key, kBytes, commentFor("0.2.0")), {publicKey(key)}, checked));
    CHECK(checked.version == v("0.2.0"));
    CHECK(checked.signature.trustedComment == "plynth-update-v1 version=0.2.0 platform=windows-x64 file=PlynthSetup.exe");
    CHECK(fileMatches(checked, hashBytes(kBytes)));
}

TEST_CASE("a changed file does not match its signature", "[update][signature]") {
    const TestKey key = makeKey();
    CheckedSignature checked;
    plynth::test::requireOk(checkSignature(sign(key, kBytes, commentFor("0.2.0")), {publicKey(key)}, checked));
    std::string changed = kBytes;
    changed[3] = static_cast<char>(changed[3] ^ 0x01);
    CHECK_FALSE(fileMatches(checked, hashBytes(changed)));
    CHECK_FALSE(fileMatches(checked, hashBytes(kBytes + "x")));
}

TEST_CASE("each changed part of a signature is refused", "[update][signature]") {
    const TestKey key = makeKey();
    const std::vector<PublicKey> keys{publicKey(key)};
    const SignatureLines good = signLines(key, kBytes, commentFor("0.2.0"));
    REQUIRE(checkCode(good.text(), keys) == ErrorCode::None);

    SECTION("the file signature") {
        SignatureLines lines = good;
        lines.signature = flipByte(good.signature, 20);
        CHECK(checkCode(lines.text(), keys) == ErrorCode::SignatureInvalid);
    }
    SECTION("the key id") {
        SignatureLines lines = good;
        lines.signature = flipByte(good.signature, 4);
        CHECK(checkCode(lines.text(), keys) == ErrorCode::SignatureInvalid);
    }
    SECTION("the trusted comment") {
        SignatureLines lines = good;
        lines.trusted = commentFor("0.3.0");
        CHECK(checkCode(lines.text(), keys) == ErrorCode::SignatureInvalid);
    }
    SECTION("the signature of the comment") {
        SignatureLines lines = good;
        lines.commentSignature = flipByte(good.commentSignature, 10);
        CHECK(checkCode(lines.text(), keys) == ErrorCode::SignatureInvalid);
    }
    SECTION("the untrusted comment is not signed, so it may change") {
        SignatureLines lines = good;
        lines.untrusted = "untrusted comment: anything at all";
        CHECK(checkCode(lines.text(), keys) == ErrorCode::None);
    }
}

TEST_CASE("the old Ed form and a wrong key are refused", "[update][signature]") {
    const TestKey key = makeKey();
    const TestKey other = makeKey();
    CHECK(checkCode(sign(key, kBytes, commentFor("0.2.0"), true), {publicKey(key)}) == ErrorCode::SignatureInvalid);
    CHECK(checkCode(sign(other, kBytes, commentFor("0.2.0")), {publicKey(key)}) == ErrorCode::SignatureInvalid);

    // A key that claims the trusted key's id still fails, because the signature does not verify with it.
    TestKey impostor = makeKey();
    impostor.keyId = key.keyId;
    CHECK(checkCode(sign(impostor, kBytes, commentFor("0.2.0")), {publicKey(key)}) == ErrorCode::SignatureInvalid);
}

TEST_CASE("missing, extra and odd lines are refused", "[update][signature]") {
    const TestKey key = makeKey();
    const std::vector<PublicKey> keys{publicKey(key)};
    const SignatureLines good = signLines(key, kBytes, commentFor("0.2.0"));
    const std::string text = good.text();

    CHECK(checkCode(text.substr(0, text.size() - 1), keys) == ErrorCode::None);
    std::string crlf;
    for (const char c : text) {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    CHECK(checkCode(crlf, keys) == ErrorCode::None);

    CHECK(checkCode(text.substr(text.find('\n') + 1), keys) == ErrorCode::SignatureInvalid);
    CHECK(checkCode(text + "extra line\n", keys) == ErrorCode::SignatureInvalid);
    CHECK(checkCode(text + "\n\n", keys) == ErrorCode::SignatureInvalid);
    CHECK(checkCode("", keys) == ErrorCode::SignatureInvalid);
    SignatureLines noPrefix = good;
    noPrefix.untrusted = "a comment without its prefix";
    CHECK(checkCode(noPrefix.text(), keys) == ErrorCode::SignatureInvalid);
    std::string noTrusted = text;
    noTrusted.replace(noTrusted.find("\ntrusted comment: ") + 1, 17, "comment: ");
    CHECK(checkCode(noTrusted, keys) == ErrorCode::SignatureInvalid);
    SignatureLines padded = good;
    padded.signature += "=";
    CHECK(checkCode(padded.text(), keys) == ErrorCode::SignatureInvalid);
}

TEST_CASE("a huge signature file is refused before it is read", "[update][signature]") {
    const TestKey key = makeKey();
    SignatureLines lines = signLines(key, kBytes, commentFor("0.2.0"));
    lines.untrusted = "untrusted comment: " + std::string(kMaxSignatureBytes, 'x');
    std::string why;
    CHECK_FALSE(parseSignature(lines.text(), why));
    CHECK(checkCode(lines.text(), {publicKey(key)}) == ErrorCode::SignatureInvalid);
}

TEST_CASE("a trusted comment for anything else is refused even when signed", "[update][signature]") {
    const TestKey key = makeKey();
    for (const char* comment : {
             "plynth-update-v2 version=0.2.0 platform=windows-x64 file=PlynthSetup.exe",
             "plynth-update-v1 version=0.2.0 platform=windows-arm64 file=PlynthSetup.exe",
             "plynth-update-v1 version=0.2.0 platform=windows-x64 file=Other.exe",
             "plynth-update-v1 platform=windows-x64 version=0.2.0 file=PlynthSetup.exe",
             "plynth-update-v1 version=0.02.0 platform=windows-x64 file=PlynthSetup.exe",
             "plynth-update-v1 version=0.2 platform=windows-x64 file=PlynthSetup.exe",
             "plynth-update-v1 version=v0.2.0 platform=windows-x64 file=PlynthSetup.exe",
             "plynth-update-v1  version=0.2.0 platform=windows-x64 file=PlynthSetup.exe",
             "plynth-update-v1 version=0.2.0 platform=windows-x64 file=PlynthSetup.exe ",
             "plynth-update-v1 version=0.2.0 platform=windows-x64 file=PlynthSetup.exe extra=1",
             "timestamp:1700000000\tfile:PlynthSetup.exe",
         }) {
        INFO(comment);
        CHECK(checkCode(sign(key, kBytes, comment), {publicKey(key)}) == ErrorCode::SignatureInvalid);
    }
}

TEST_CASE("one to three keys are trusted, and none trusts nothing", "[update][signature]") {
    const TestKey a = makeKey();
    const TestKey b = makeKey();
    const TestKey c = makeKey();
    const std::vector<PublicKey> three{publicKey(a), publicKey(b), publicKey(c)};
    for (const TestKey* key : {&a, &b, &c}) {
        CHECK(checkCode(sign(*key, kBytes, commentFor("0.2.0")), three) == ErrorCode::None);
        CHECK(checkCode(sign(*key, kBytes, commentFor("0.2.0")), {publicKey(*key)}) == ErrorCode::None);
    }
    CHECK(checkCode(sign(a, kBytes, commentFor("0.2.0")), {}) == ErrorCode::SignatureInvalid);
}

TEST_CASE("the trusted keys file keeps key lines only and at most three", "[update][signature]") {
    const TestKey a = makeKey();
    const TestKey b = makeKey();
    const TestKey c = makeKey();
    const TestKey d = makeKey();
    CHECK(parseTrustedKeys("").empty());
    CHECK(parseTrustedKeys("# Trusted minisign public keys. None yet.\n").empty());

    const std::string one = "untrusted comment: minisign public key " + keyIdText(a.keyId) + "\r\n" + publicKeyLine(a) + "\r\n";
    const auto parsed = parseTrustedKeys(one);
    REQUIRE(parsed.size() == 1);
    CHECK(parsed[0].keyId == a.keyId);
    CHECK(parsed[0].key == publicKey(a).key);

    const std::string four = "# keys\n" + publicKeyLine(a) + "\n\n  " + publicKeyLine(b) + "  \nnot a key\n" +
                             publicKeyLine(c) + "\n" + publicKeyLine(d) + "\n";
    const auto kept = parseTrustedKeys(four);
    REQUIRE(kept.size() == 3);
    CHECK(kept[2].keyId == c.keyId);
}

TEST_CASE("the built in keys are exactly those of the trusted keys file", "[update][signature]") {
    const std::string text = readBytes(fs::path(PLYNTH_SOURCE_DIR) / "core" / "src" / "update" / "trusted-keys.txt");
    const auto fromFile = parseTrustedKeys(text);
    REQUIRE(builtInKeys().size() == fromFile.size());
    CHECK(builtInKeys().size() <= kMaxTrustedKeys);
    for (std::size_t i = 0; i < fromFile.size(); ++i) {
        CHECK(builtInKeys()[i].keyId == fromFile[i].keyId);
        CHECK(builtInKeys()[i].key == fromFile[i].key);
    }
}

TEST_CASE("a key id is printed as minisign prints it", "[update][signature]") {
    const std::array<unsigned char, 8> id{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0xab};
    CHECK(keyIdText(id) == "AB07060504030201");
    CHECK_FALSE(parsePublicKey("RWQ"));
    CHECK_FALSE(parsePublicKey(""));
}

TEST_CASE("versions and tags are read strictly", "[update][version]") {
    CHECK(parseVersion("0.1.0") == Version{0, 1, 0});
    CHECK(parseVersion("10.20.30") == Version{10, 20, 30});
    CHECK(parseVersion("999999999.0.0") == Version{999999999, 0, 0});
    for (const char* bad : {"", "1", "1.2", "1.2.3.4", "01.2.3", "1.02.3", "1.2.03", "1.2.3-beta", "1.2.3 ", " 1.2.3",
                            "v1.2.3", "1..3", "1.2.", "a.b.c", "1000000000.0.0", "-1.2.3", "+1.2.3"}) {
        INFO(bad);
        CHECK_FALSE(parseVersion(bad));
    }
    CHECK(parseTag("v0.2.0") == Version{0, 2, 0});
    CHECK_FALSE(parseTag("0.2.0"));
    CHECK_FALSE(parseTag("V0.2.0"));
    CHECK_FALSE(parseTag("v0.2.0-rc1"));
    CHECK_FALSE(parseTag("v"));
    CHECK(versionText(Version{1, 22, 333}) == "1.22.333");
}

TEST_CASE("versions compare by number, so 0.10 is above 0.9", "[update][version]") {
    CHECK(v("0.10.0") > v("0.9.0"));
    CHECK(v("1.0.0") > v("0.99.99"));
    CHECK(v("0.1.1") > v("0.1.0"));
    CHECK(v("0.2.0") == v("0.2.0"));
}

TEST_CASE("the trusted comment is written and read back exactly", "[update][version]") {
    CHECK(trustedCommentFor(v("0.1.0")) == "plynth-update-v1 version=0.1.0 platform=windows-x64 file=PlynthSetup.exe");
    CHECK(parseTrustedComment(trustedCommentFor(v("3.4.5"))) == v("3.4.5"));
    CHECK_FALSE(parseTrustedComment(""));
    CHECK_FALSE(parseTrustedComment("plynth-update-v1 version=0.1.0"));
}

TEST_CASE("the version resource of a program is read from its bytes", "[update][version]") {
    CHECK(programVersion(fakeInstaller()) == v("0.2.0"));
    CHECK_FALSE(programVersion(""));
    CHECK_FALSE(programVersion("MZ"));
    CHECK_FALSE(programVersion(std::string(4096, '\0')));
    const std::string bytes = fakeInstaller();
    CHECK_FALSE(programVersion(bytes.substr(0, 512)));
}
