#include <plynth/core/SeededChoice.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

using namespace plynth;

namespace {

// The hash written out a second way, byte by byte from a list, so a change to the code is caught.
std::uint64_t referenceScore(std::uint32_t seed, const std::string& key, const std::string& candidate)
{
    std::vector<unsigned char> bytes = {static_cast<unsigned char>(seed & 0xff), static_cast<unsigned char>((seed >> 8) & 0xff),
                                        static_cast<unsigned char>((seed >> 16) & 0xff),
                                        static_cast<unsigned char>((seed >> 24) & 0xff)};
    bytes.insert(bytes.end(), key.begin(), key.end());
    bytes.push_back(0);
    bytes.insert(bytes.end(), candidate.begin(), candidate.end());
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char b : bytes) {
        h ^= b;
        h *= 0x100000001b3ull;
    }
    h ^= h >> 30;
    h *= 0xbf58476d1ce4e5b9ull;
    h ^= h >> 27;
    h *= 0x94d049bb133111ebull;
    h ^= h >> 31;
    return h;
}

// The id a choice picks, empty text for none.
std::string picked(std::uint32_t seed, const std::string& key, const std::vector<std::string>& ids)
{
    const auto index = seededChoice(seed, key, ids);
    return index ? ids[*index] : std::string();
}

}

TEST_CASE("Choice scores follow the fixed hash", "[choice]")
{
    for (std::uint32_t seed : {0u, 1u, 7u, 0xffffffffu}) {
        for (const char* key : {"", "f1/i3/asset", "r1/floor"}) {
            for (const char* candidate : {"", "sofa_01", "oak_planks"}) {
                CHECK(choiceScore(seed, key, candidate) == referenceScore(seed, key, candidate));
            }
        }
    }
    // Values seen on the first GitHub run, kept so the hash never changes silently.
    CHECK(choiceScore(0, "f1/i3/asset", "sofa_01") == 5380586521416820386ull);
    CHECK(choiceScore(7, "r1/floor", "oak_planks") == 7759767856783350913ull);
    CHECK(choiceScore(0xffffffffu, "", "") == 12780864653111665887ull);
    // The key and the candidate are kept apart by the zero byte.
    CHECK(choiceScore(1, "ab", "c") != choiceScore(1, "a", "bc"));
    CHECK(choiceScore(1, "k", "a") != choiceScore(2, "k", "a"));
}

TEST_CASE("The order of the candidates does not change a choice", "[choice]")
{
    std::vector<std::string> ids = {"a", "b", "c", "d", "e"};
    for (std::uint32_t seed = 0; seed < 50; ++seed) {
        const std::string first = picked(seed, "key", ids);
        std::vector<std::string> shuffled = ids;
        std::reverse(shuffled.begin(), shuffled.end());
        CHECK(picked(seed, "key", shuffled) == first);
        std::rotate(shuffled.begin(), shuffled.begin() + 2, shuffled.end());
        CHECK(picked(seed, "key", shuffled) == first);
    }
}

TEST_CASE("An added candidate only takes choices for itself", "[choice]")
{
    const std::vector<std::string> ids = {"a", "b", "c"};
    std::vector<std::string> more = ids;
    more.push_back("z");
    int taken = 0;
    for (std::uint32_t seed = 0; seed < 200; ++seed) {
        const std::string before = picked(seed, "k", ids);
        const std::string after = picked(seed, "k", more);
        CHECK((after == before || after == "z"));
        if (after == "z") ++taken;
    }
    CHECK(taken > 0);
}

TEST_CASE("Seeds spread choices evenly", "[choice]")
{
    const std::vector<std::string> ids = {"chair_a", "chair_b", "chair_c", "chair_d"};
    std::map<std::string, int> counts;
    for (std::uint32_t seed = 1; seed <= 1000; ++seed) ++counts[picked(seed, "f1/i1/asset", ids)];
    for (const std::string& id : ids) {
        INFO(id << " picked " << counts[id] << " times");
        CHECK(counts[id] >= 200);
        CHECK(counts[id] <= 300);
    }
}

TEST_CASE("An empty list gives no choice", "[choice]")
{
    const std::vector<std::string> none;
    CHECK_FALSE(seededChoice(3, "k", none).has_value());
}

TEST_CASE("Seeded units stay from 0 up to 1 and repeat", "[choice]")
{
    double low = 1.0;
    double high = 0.0;
    for (std::uint32_t seed = 0; seed < 2000; ++seed) {
        const double u = seededUnit(seed, "unit");
        CHECK(u >= 0.0);
        CHECK(u < 1.0);
        CHECK(seededUnit(seed, "unit") == u);
        low = std::min(low, u);
        high = std::max(high, u);
    }
    CHECK(low < 0.05);
    CHECK(high > 0.95);
}
