#include "plynth/core/SeededChoice.h"

namespace plynth {

namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

// Mixes one byte into an FNV-1a hash.
std::uint64_t mixByte(std::uint64_t hash, unsigned char byte)
{
    return (hash ^ byte) * kFnvPrime;
}

// The splitmix64 finishing step, which spreads small differences over every bit.
std::uint64_t finish(std::uint64_t z)
{
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

}

// Scores one candidate for one choice.
std::uint64_t choiceScore(std::uint32_t seed, std::string_view key, std::string_view candidate)
{
    std::uint64_t hash = kFnvOffset;
    for (int i = 0; i < 4; ++i) hash = mixByte(hash, static_cast<unsigned char>((seed >> (8 * i)) & 0xffu));
    for (char c : key) hash = mixByte(hash, static_cast<unsigned char>(c));
    hash = mixByte(hash, 0);
    for (char c : candidate) hash = mixByte(hash, static_cast<unsigned char>(c));
    return finish(hash);
}

// Picks the candidate with the highest score, ties to the smaller id.
std::optional<std::size_t> seededChoice(std::uint32_t seed, std::string_view key, std::span<const std::string> ids)
{
    std::optional<std::size_t> best;
    std::uint64_t bestScore = 0;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const std::uint64_t score = choiceScore(seed, key, ids[i]);
        if (!best || score > bestScore || (score == bestScore && ids[i] < ids[*best])) {
            best = i;
            bestScore = score;
        }
    }
    return best;
}

// A repeatable number from 0 up to 1.
double seededUnit(std::uint32_t seed, std::string_view key)
{
    const std::uint64_t bits = choiceScore(seed, key, {}) >> 11;
    return static_cast<double>(bits) * (1.0 / 9007199254740992.0);
}

}
