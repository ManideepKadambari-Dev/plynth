#pragma once

#include "plynth/core/Status.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

inline constexpr std::string_view kTourFormat = "plynth-tour";
inline constexpr int kTourVersion = 1;
inline constexpr std::size_t kMaxTourTextLength = 240;
inline constexpr std::size_t kMaxTourNameLength = 60;

// The name and explanation of one control. An id ending in "*" covers every id that starts with the rest.
struct TourEntry {
    std::string id;
    std::string name;
    std::string text;

    bool operator==(const TourEntry&) const = default;
};

// One stop of a tour. screen is one of tourScreens().
struct TourStep {
    std::string control;
    std::string screen;

    bool operator==(const TourStep&) const = default;
};

struct TourInfo {
    std::string id;
    std::string name;
    std::vector<TourStep> steps;

    bool operator==(const TourInfo&) const = default;
};

struct TourData {
    std::vector<TourEntry> controls;
    std::vector<TourInfo> tours;

    bool operator==(const TourData&) const = default;
};

struct TourRead {
    Status status; // BadFile with where set, UnsupportedVersion
    TourData data;
};

// Reads tour.json text. Refuses a bad format or version, a repeated id, an empty or too long name or text,
// more than two sentences, a dash used as punctuation, an unknown screen, a step whose control has no entry
// and a tour with no steps.
TourRead readTour(std::string_view jsonText);

// The entry for a control: the exact id, else the longest key ending in "*" that the id starts with.
// Null when none fits.
const TourEntry* tourEntry(const TourData& data, std::string_view controlId);

// The tour with this id, or null.
const TourInfo* findTour(const TourData& data, std::string_view tourId);

// The five tours in Help menu order: plan, wall, build_export, assets, materials.
std::span<const std::string_view> tourIds();

// The screens a step may name: home, plan, wall, build, export, assets, materials.
std::span<const std::string_view> tourScreens();

// The step to show next going +1 or -1 from 'from' (none means from the start for +1 and from the end for
// -1), skipping steps whose control is not present. Empty when there is none.
std::optional<std::size_t> tourStep(const TourInfo& tour, std::optional<std::size_t> from, int direction,
                                    const std::function<bool(const TourStep&)>& present);

struct TourCheck {
    std::size_t withoutId = 0;             // controls the audit found with no id
    std::vector<std::string> withoutEntry; // ids with no entry, sorted, each once
    std::vector<std::string> unused;       // entries no control matches, sorted
    std::vector<std::string> dialogsNotOpened;

    bool ok() const { return withoutId == 0 && withoutEntry.empty() && unused.empty() && dialogsNotOpened.empty(); }
    bool operator==(const TourCheck&) const = default;
};

// Compares the control ids the app's audit listed with the entries. ok() means the check passes.
TourCheck checkTourEntries(const TourData& data, std::span<const std::string> controlIds,
                           std::size_t controlsWithoutId, std::span<const std::string> dialogsExpected,
                           std::span<const std::string> dialogsOpened);

}
