#include "UpdateInternals.h"

#include <charconv>

namespace plynth::update_detail {

namespace {

// Reads one part of a version: 1 to 9 digits, no leading zero unless it is just "0".
std::optional<unsigned> readPart(std::string_view text)
{
    if (text.empty() || text.size() > 9) return std::nullopt;
    if (text.size() > 1 && text[0] == '0') return std::nullopt;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
    }
    unsigned value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

}

// Reads "1.2.3" strictly.
std::optional<Version> parseVersion(std::string_view text)
{
    const std::size_t first = text.find('.');
    if (first == std::string_view::npos) return std::nullopt;
    const std::size_t second = text.find('.', first + 1);
    if (second == std::string_view::npos) return std::nullopt;
    const auto major = readPart(text.substr(0, first));
    const auto minor = readPart(text.substr(first + 1, second - first - 1));
    const auto patch = readPart(text.substr(second + 1));
    if (!major || !minor || !patch) return std::nullopt;
    return Version{*major, *minor, *patch};
}

// Reads "v1.2.3" strictly.
std::optional<Version> parseTag(std::string_view tag)
{
    if (tag.size() < 2 || tag[0] != 'v') return std::nullopt;
    return parseVersion(tag.substr(1));
}

// Writes "1.2.3".
std::string versionText(const Version& version)
{
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
}

// The exact text the owner signs for a version.
std::string trustedCommentFor(const Version& version)
{
    return "plynth-update-v1 version=" + versionText(version) + " platform=windows-x64 file=PlynthSetup.exe";
}

// Reads a trusted comment by finding the version and then comparing the whole text with the one it must be.
std::optional<Version> parseTrustedComment(std::string_view comment)
{
    constexpr std::string_view prefix = "plynth-update-v1 version=";
    if (comment.substr(0, prefix.size()) != prefix) return std::nullopt;
    const std::string_view rest = comment.substr(prefix.size());
    const auto version = parseVersion(rest.substr(0, rest.find(' ')));
    if (!version || trustedCommentFor(*version) != comment) return std::nullopt;
    return version;
}

}
