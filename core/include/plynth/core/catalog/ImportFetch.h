#pragma once

#include "../Blender.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// A Blender job that brings a store asset onto disk before the usual cleanup. An importer
// whose source is not a file yet, such as a BlenderKit address, puts one of these in its plan. The asset or
// material import then runs it first (runImportFetch in StoreFetch.h), and the cleanup reads the file it
// fetched. The fetch is a separate job because it must run with the user's own add-ons and sign in
// (factory startup off), while the cleanup always runs with factory startup so add-ons cannot change it.
//
// Plynth itself never talks to a store. A fetch task only uses the store's own Blender add-on, which the
// user installed and signed in to, never a private or undocumented store endpoint.

// What kind of item the fetch must bring: kFetchKindModel for the assets catalog, kFetchKindMaterial for
// the materials catalog.
inline constexpr std::string_view kFetchKindModel = "model";
inline constexpr std::string_view kFetchKindMaterial = "material";

// The folder inside the import's work folder that the fetch task writes into.
inline constexpr std::string_view kFetchOutputFolder = "fetched";

struct ImportFetch {
    std::string task;         // a worker task id, for example kBlenderKitFetchTask in BlenderKit.h
    Json params;              // the task's params without output_folder, which runImportFetch adds
    std::string expectedKind; // kFetchKindModel or kFetchKindMaterial
    std::chrono::seconds timeout{720}; // for the whole Blender run, a little more than the task's own wait
    // The task's steps in order. runImportFetch puts them in BlenderRunOptions::steps: nothing else the fetch
    // prints is shown or kept.
    std::vector<BlenderStep> steps;
};

}
