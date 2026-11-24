#pragma once

#include "plynth/core/Status.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

inline constexpr std::string_view kSamplesIndexFileName = "samples.json";
inline constexpr std::string_view kSamplesFormat = "plynth-samples";
inline constexpr int kSamplesVersion = 1;

struct SampleAsset {
    std::string id;
    std::string name;
    std::string file; // relative to the samples folder
    std::vector<std::string> categories;
    std::vector<std::string> tags;

    bool operator==(const SampleAsset&) const = default;
};

struct SampleMaterial {
    std::string id;
    std::string folder;

    bool operator==(const SampleMaterial&) const = default;
};

struct SampleProject {
    std::string id;
    std::string name;
    std::string description;
    std::string folder;
    std::string image; // a picture for Home, relative to the samples folder

    bool operator==(const SampleProject&) const = default;
};

struct SamplesIndex {
    std::string license;
    std::vector<SampleAsset> assets;
    std::vector<SampleMaterial> materials;
    std::vector<SampleProject> projects;

    bool operator==(const SamplesIndex&) const = default;
};

struct SamplesRead {
    Status status; // CannotRead, BadFile with where set, UnsupportedVersion
    SamplesIndex index;
};

// Reads samples.json of a samples folder. Refuses a bad format or version, a bad or repeated id, and a path
// that is absolute or leaves the samples folder.
SamplesRead readSamples(const std::filesystem::path& samplesFolder);

// Reads samples.json text. The same rules as readSamples.
SamplesRead parseSamples(std::string_view jsonText);

struct SampleCopy {
    Status status; // NotFound for an unknown id, InvalidValue for a target inside the samples, CannotWrite
    std::filesystem::path projectFolder;
};

// Copies one sample project into a new folder below into, named after the sample: "Apartment", then
// "Apartment (2)". Only project.json, plan.json, export-options.json and plans/ are copied. Never overwrites
// and leaves nothing behind after a failure.
SampleCopy copySampleProject(const std::filesystem::path& samplesFolder, std::string_view id,
                             const std::filesystem::path& into);

}
