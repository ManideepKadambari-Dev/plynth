#include "GuideSupport.h"

#include <catch2/catch_test_macros.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

// The SHA-256 of a file's bytes as lower case hex. Empty when it cannot be worked out.
std::string sha256Hex(const std::string& bytes)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
    std::vector<unsigned char> digest(32);
    const NTSTATUS result = BCryptHash(algorithm, nullptr, 0,
                                       reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                                       static_cast<ULONG>(bytes.size()), digest.data(), static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (result != 0) return {};
    std::string hex;
    char part[3];
    for (unsigned char b : digest) {
        std::snprintf(part, sizeof part, "%02x", b);
        hex += part;
    }
    return hex;
}

}

TEST_CASE("The logo and the icon are unchanged", "[logo]")
{
    const std::filesystem::path resources = std::filesystem::path(PLYNTH_APP_RESOURCES_DIR);
    const std::string logo = plynth::test::readFile(resources / "logo.png");
    const std::string icon = plynth::test::readFile(resources / "plynth.ico");
    REQUIRE(!logo.empty());
    REQUIRE(!icon.empty());
    CHECK(sha256Hex(logo) == "b710522129cffb34fcf614c45d9fa13661166a41fbbe978c1fa3113fbbff89aa");
    CHECK(sha256Hex(icon) == "94551d8ea0fb7d48b2745dd5540a8db381be0428bc3edde990ff32b1c6538746");
}
