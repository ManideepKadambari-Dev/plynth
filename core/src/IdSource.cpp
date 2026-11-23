#include "IdSource.h"

#include "plynth/core/Plan.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <utility>

namespace plynth::detail {

namespace {

std::atomic<bool> counted{false};

// The test source shared by every thread, and the lock that guards it. An empty maker means the product source.
struct SharedMaker {
    std::mutex lock;
    IdPartMaker maker;
};

// The one shared maker of the process.
SharedMaker& sharedMaker()
{
    static SharedMaker shared;
    return shared;
}

// Turns one 64 bit output into a part: a digit from the top 32 bits, then seven characters from the low 35 bits.
std::string partFrom(std::uint64_t v)
{
    std::string part(1, static_cast<char>('0' + (((v >> 32) * 10) >> 32)));
    for (unsigned k = 0; k + 1 < kNewIdLength; ++k) part += kIdAlphabet[(v >> (5 * k)) & 31];
    return part;
}

// Splits a 64 bit value into its two 32 bit halves for a seed list.
void addWords(std::vector<std::uint32_t>& words, std::uint64_t value)
{
    words.push_back(static_cast<std::uint32_t>(value));
    words.push_back(static_cast<std::uint32_t>(value >> 32));
}

// Makes this thread's engine, seeded so that two processes or threads never share a sequence.
std::mt19937_64 seededEngine()
{
    std::vector<std::uint32_t> words;
    addWords(words, static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::random_device device;
        words.push_back(device());
        words.push_back(device());
    } catch (...) {
        addWords(words, static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    }
    std::seed_seq seed(words.begin(), words.end());
    return std::mt19937_64(seed);
}

// Says whether a part has the shape of a new id: a digit, then characters of kIdAlphabet with at least one letter.
bool hasShape(std::string_view part)
{
    if (part.size() != kNewIdLength || part[0] < '0' || part[0] > '9') return false;
    if (part.find_first_not_of(kIdAlphabet) != std::string_view::npos) return false;
    return part.substr(1).find_first_not_of("0123456789") != std::string_view::npos;
}

// The next part from the test source when one is set, otherwise from the product source.
std::string nextPart()
{
    SharedMaker& shared = sharedMaker();
    std::lock_guard<std::mutex> guard(shared.lock);
    if (shared.maker) return shared.maker();
    return systemIdPart();
}

}

// One random part from this thread's engine.
std::string systemIdPart()
{
    thread_local std::mt19937_64 engine = seededEngine();
    return partFrom(engine());
}

// Draws parts until one has the shape and is free, at most kMaxIdDraws times.
std::string newIdPart(const std::function<bool(std::string_view part)>& taken)
{
    for (int draw = 0; draw < kMaxIdDraws; ++draw) {
        const std::string part = nextPart();
        if (hasShape(part) && !taken(part)) return part;
    }
    return {};
}

// Says whether tests asked for counted ids.
bool countedIds()
{
    return counted.load();
}

// Switches counted ids on and remembers what was there before.
ScopedCountedIds::ScopedCountedIds() : previous_(counted.exchange(true)) {}

// Puts back what was there before.
ScopedCountedIds::~ScopedCountedIds()
{
    counted.store(previous_);
}

// Sets the test source and switches counting off, remembering both.
ScopedIdSource::ScopedIdSource(IdPartMaker maker) : previousCounted_(counted.exchange(false))
{
    SharedMaker& shared = sharedMaker();
    std::lock_guard<std::mutex> guard(shared.lock);
    previousMaker_ = std::exchange(shared.maker, std::move(maker));
}

// Puts back the source and the counting that were there before.
ScopedIdSource::~ScopedIdSource()
{
    {
        SharedMaker& shared = sharedMaker();
        std::lock_guard<std::mutex> guard(shared.lock);
        shared.maker = std::move(previousMaker_);
    }
    counted.store(previousCounted_);
}

// Parts from an engine with a fixed seed. Copies of the maker share the engine.
IdPartMaker seededIdParts(std::uint64_t seed)
{
    auto engine = std::make_shared<std::mt19937_64>(seed);
    return [engine]() { return partFrom((*engine)()); };
}

// The listed parts first, then parts seeded with 0. Copies of the maker share their place in the list.
IdPartMaker scriptedIdParts(std::vector<std::string> parts)
{
    struct Script {
        std::vector<std::string> parts;
        std::size_t next = 0;
        IdPartMaker after = seededIdParts(0);
    };
    auto script = std::make_shared<Script>();
    script->parts = std::move(parts);
    return [script]() {
        if (script->next < script->parts.size()) return script->parts[script->next++];
        return script->after();
    };
}

}
