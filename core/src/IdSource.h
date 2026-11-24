#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace plynth::detail {

// Gives the kNewIdLength characters after a prefix, for one try at a new id.
using IdPartMaker = std::function<std::string()>;

// One random part from the product's source.
std::string systemIdPart();

// Makes the part of a new id: tries parts of the right shape until taken says no. Empty after kMaxIdDraws tries.
std::string newIdPart(const std::function<bool(std::string_view part)>& taken);

// Says whether new ids are counted the old way right now, on any thread. Only tests switch it on.
bool countedIds();

// While it lives, new ids are counted as they were before random ids, on every thread of the process. Tests only.
class ScopedCountedIds {
public:
    ScopedCountedIds();
    ~ScopedCountedIds();
    ScopedCountedIds(const ScopedCountedIds&) = delete;
    ScopedCountedIds& operator=(const ScopedCountedIds&) = delete;

private:
    bool previous_;
};

// While it lives, new ids on every thread of the process are random with their parts from maker, also inside a
// ScopedCountedIds. Tests only.
class ScopedIdSource {
public:
    explicit ScopedIdSource(IdPartMaker maker);
    ~ScopedIdSource();
    ScopedIdSource(const ScopedIdSource&) = delete;
    ScopedIdSource& operator=(const ScopedIdSource&) = delete;

private:
    IdPartMaker previousMaker_;
    bool previousCounted_;
};

// Parts from a std::mt19937_64 with a fixed seed, one output each, the same in every run and with every compiler.
IdPartMaker seededIdParts(std::uint64_t seed);

// The given parts in order, then seededIdParts(0).
IdPartMaker scriptedIdParts(std::vector<std::string> parts);

}
