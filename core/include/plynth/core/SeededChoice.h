#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace plynth {

// Repeatable choices for variations (DESIGN.md section 7.15). Every choice comes from a fixed hash, never from
// rand or a standard library distribution, so a seed gives the same result on every machine and with every
// compiler.
//
// The score of a candidate is the 64 bit FNV-1a hash of the seed (4 bytes, lowest first), the key, one zero
// byte and the candidate id, finished with the splitmix64 mixing step. The key says what is being chosen, for
// example "f1/i3/asset", so two choices with the same seed do not follow each other.

// The score of one candidate for one choice.
std::uint64_t choiceScore(std::uint32_t seed, std::string_view key, std::string_view candidate);

// Picks one of the ids: the one with the highest score, ties going to the smaller id. The order of the list
// does not matter, and adding a candidate only takes choices for itself. Empty for an empty list. The index is
// into ids.
std::optional<std::size_t> seededChoice(std::uint32_t seed, std::string_view key, std::span<const std::string> ids);

// A repeatable number from 0 up to (not including) 1 for a seed and key: the top 53 bits of choiceScore with
// an empty candidate.
double seededUnit(std::uint32_t seed, std::string_view key);

}
