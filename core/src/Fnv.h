#pragma once

#include <cstdint>
#include <string_view>

namespace plynth::detail {

// The published 64 bit FNV-1a hash of some text.
constexpr std::uint64_t fnv1a(std::string_view text)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

}
