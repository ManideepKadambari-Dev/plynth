#include "UpdateInternals.h"

#include "PlynthTrustedKeys.h"

#include <sodium.h>

#include <cstring>

namespace plynth::update_detail {

namespace {

// Starts libsodium once. False when it cannot run on this machine.
bool sodiumReady()
{
    static const bool ready = sodium_init() >= 0;
    return ready;
}

// Decodes standard base64 with padding into exactly size bytes. False for anything else.
bool decodeExact(std::string_view text, unsigned char* out, std::size_t size)
{
    if (!sodiumReady() || text.empty()) return false;
    std::size_t length = 0;
    const char* end = nullptr;
    if (sodium_base642bin(out, size, text.data(), text.size(), nullptr, &length, &end, sodium_base64_VARIANT_ORIGINAL) != 0) {
        return false;
    }
    return length == size && end == text.data() + text.size();
}

// Splits text into lines, taking off one "\r" at the end of each.
std::vector<std::string_view> splitLines(std::string_view text)
{
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        std::string_view line = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return lines;
}

// Says whether a line starts with a prefix.
bool startsWith(std::string_view line, std::string_view prefix)
{
    return line.substr(0, prefix.size()) == prefix;
}

}

// The key id as 16 upper case hex digits, read as minisign does (little endian).
std::string keyIdText(const std::array<unsigned char, 8>& keyId)
{
    static const char digits[] = "0123456789ABCDEF";
    std::string text;
    for (std::size_t i = keyId.size(); i-- > 0;) {
        text += digits[keyId[i] >> 4];
        text += digits[keyId[i] & 0x0f];
    }
    return text;
}

// Reads one public key line: base64 of "Ed", the key id and the 32 byte key.
std::optional<PublicKey> parsePublicKey(std::string_view line)
{
    unsigned char raw[2 + 8 + 32];
    if (!decodeExact(line, raw, sizeof raw)) return std::nullopt;
    if (raw[0] != 'E' || raw[1] != 'd') return std::nullopt;
    PublicKey key;
    std::memcpy(key.keyId.data(), raw + 2, 8);
    std::memcpy(key.key.data(), raw + 10, 32);
    return key;
}

// Reads the trusted keys file.
std::vector<PublicKey> parseTrustedKeys(std::string_view text)
{
    std::vector<PublicKey> keys;
    for (std::string_view line : splitLines(text)) {
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
        if (line.empty() || line[0] == '#' || startsWith(line, "untrusted comment:")) continue;
        if (auto key = parsePublicKey(line); key && keys.size() < kMaxTrustedKeys) keys.push_back(*key);
    }
    return keys;
}

// The keys compiled in.
const std::vector<PublicKey>& builtInKeys()
{
    static const std::vector<PublicKey> keys = parseTrustedKeys(kTrustedKeysText);
    return keys;
}

// Reads the four lines of a .minisig file.
std::optional<Signature> parseSignature(std::string_view text, std::string& why)
{
    if (text.size() > kMaxSignatureBytes) {
        why = "The signature file is too big.";
        return std::nullopt;
    }
    std::vector<std::string_view> lines = splitLines(text);
    if (!lines.empty() && lines.back().empty()) lines.pop_back();
    if (lines.size() != 4) {
        why = "The signature file does not have exactly four lines.";
        return std::nullopt;
    }
    if (!startsWith(lines[0], "untrusted comment: ")) {
        why = "The signature file does not start with an untrusted comment.";
        return std::nullopt;
    }
    unsigned char raw[2 + 8 + 64];
    if (!decodeExact(lines[1], raw, sizeof raw)) {
        why = "The signature line is not valid.";
        return std::nullopt;
    }
    if (raw[0] != 'E' || raw[1] != 'D') {
        why = "The signature is not in the prehashed form.";
        return std::nullopt;
    }
    constexpr std::string_view trustedPrefix = "trusted comment: ";
    if (!startsWith(lines[2], trustedPrefix)) {
        why = "The signature file has no trusted comment.";
        return std::nullopt;
    }
    Signature signature;
    std::memcpy(signature.keyId.data(), raw + 2, 8);
    std::memcpy(signature.fileSignature.data(), raw + 10, 64);
    signature.trustedComment = std::string(lines[2].substr(trustedPrefix.size()));
    if (!decodeExact(lines[3], signature.commentSignature.data(), signature.commentSignature.size())) {
        why = "The signature of the trusted comment is not valid.";
        return std::nullopt;
    }
    return signature;
}

// Checks a .minisig text: form, key, comment signature and comment text.
Status checkSignature(std::string_view text, const std::vector<PublicKey>& keys, CheckedSignature& out)
{
    if (!sodiumReady()) return fail(ErrorCode::SignatureInvalid, "The signature check cannot run on this computer.");
    std::string why;
    const auto signature = parseSignature(text, why);
    if (!signature) return fail(ErrorCode::SignatureInvalid, why);
    const PublicKey* key = nullptr;
    for (const PublicKey& candidate : keys) {
        if (candidate.keyId == signature->keyId) key = &candidate;
    }
    if (!key) return fail(ErrorCode::SignatureInvalid, "The release was signed with a key this Plynth does not trust.");

    std::string message(signature->fileSignature.begin(), signature->fileSignature.end());
    message += signature->trustedComment;
    if (crypto_sign_verify_detached(signature->commentSignature.data(),
                                    reinterpret_cast<const unsigned char*>(message.data()), message.size(),
                                    key->key.data()) != 0) {
        return fail(ErrorCode::SignatureInvalid, "The signature of the trusted comment does not verify.");
    }
    const auto version = parseTrustedComment(signature->trustedComment);
    if (!version) return fail(ErrorCode::SignatureInvalid, "The signed comment is not one for a Plynth update.");
    out = CheckedSignature{*signature, *key, *version};
    return {};
}

// Checks the file signature against a hash.
bool fileMatches(const CheckedSignature& checked, const FileHash& hash)
{
    if (!sodiumReady()) return false;
    return crypto_sign_verify_detached(checked.signature.fileSignature.data(), hash.data(), hash.size(),
                                       checked.key.key.data()) == 0;
}

// BLAKE2b-512 of bytes.
FileHash hashBytes(std::string_view bytes)
{
    FileHash hash{};
    if (!sodiumReady()) return hash;
    crypto_generichash(hash.data(), hash.size(), reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(),
                       nullptr, 0);
    return hash;
}

}
