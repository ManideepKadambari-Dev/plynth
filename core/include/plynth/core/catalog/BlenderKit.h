#pragma once

#include "../Status.h"
#include "ImportFetch.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace plynth {

// BlenderKit import. The user pastes an item's address; core reads
// the item's id from it, and Blender downloads the item through the BlenderKit add-on the user installed and
// signed in to inside Blender. Plynth never calls a BlenderKit web address itself, never shows or copies the
// add-on's key, and never touches the add-on's own download folder except to copy the downloaded files out of it.
// Downloaded items are for the user's own use and are never bundled or put in packs.
//
// Registry entries, one file each in core/src/catalog/:
//   importers/BlenderKitImporter.cpp            asset importer kBlenderKitImporterId and the integration
//                                               kBlenderKitIntegrationId (Integrations.h)
//   material_importers/BlenderKitMaterialImporter.cpp   material importer kBlenderKitImporterId
// Both take addresses only (no file endings). Score: 30 for an address parseBlenderKitAddress accepts, 0 for
// anything else, files included. Plan: no file, fetch = makeBlenderKitFetch with the integration's options,
// sourceKind kSourceBlenderKit, reference the address as pasted (trimmed), no default name or license (the
// fetch reports them), originalIsZip false, details {"asset_base_id": "..."}.

inline constexpr std::string_view kBlenderKitImporterId = "blenderkit";
inline constexpr std::string_view kBlenderKitIntegrationId = "blenderkit";
inline constexpr std::string_view kBlenderKitSite = "https://www.blenderkit.com/";

// The worker task (blender/plynth/tasks/blenderkit_fetch.py).
// Params: {"asset_base_id": "0c1d4a2e-....", "kind": "model" or "material", "resolution": "2k",
//          "output_folder": "D:/Library/assets/.staging/blenderkit-3f9a07c1/fetched", "wait_seconds": 600}
// Steps: "check_addon" (installed, enabled and a key stored, the key itself never read out), "download" (the
// add-on's own download operator, scene.blenderkit_download, with the resolution when the operator offers
// one; in background mode the worker runs the add-on's timer functions while it waits), "copy" (the new
// .blend and the files beside it that it uses, from the add-on's download folder into output_folder).
// Output: FetchOutput of StoreFetch.h with format "blend".
// Worker error codes: addon_missing (not installed or not enabled), not_signed_in, download_failed (the
// add-on reported an error, or no file arrived and no download is running), download_timeout (nothing after
// wait_seconds), wrong_asset_kind (the item is not of the kind asked for: a model or a material gets the
// sentence of applyFetch naming the right tab; for any other, for example an HDR or a brush, the message
// names its kind).
inline constexpr std::string_view kBlenderKitFetchTask = "blenderkit_fetch";

// Texture resolutions the add-on can download. "original" takes the files as the author uploaded them.
// 0.5k 1k 2k 4k 8k original
bool isBlenderKitResolution(std::string_view resolution);

struct BlenderKitAddress {
    Status status;           // InvalidValue with a plain sentence when the text is not a BlenderKit address
    std::string assetBaseId; // lower case, 8-4-4-4-12 hex digits
};

// Reads the item id from what the user pasted, after trimming spaces:
// - an https or http address on blenderkit.com or www.blenderkit.com whose path holds one id (8-4-4-4-12
//   hex digits), for example https://www.blenderkit.com/get-blenderkit/<id>/ or .../asset-gallery-detail/<id>/
// - the text "asset_base_id:<id>" as the add-on's own search box takes it
// - a bare id
// Refusals: another site "This is not a BlenderKit address."; no id or two different ids "The address does not
// name one BlenderKit item. Copy it from the item's page.".
BlenderKitAddress parseBlenderKitAddress(std::string_view text);

// The options of the BlenderKit integration, saved in integrations.json (Integrations.h):
//   "blender_window": no longer offered. "none" and "minimized" are still read and ignored, so an older
//                     saved file loads; a fetch always runs with --background.
//   "resolution":     one of isBlenderKitResolution, default "2k"
//   "wait_seconds":   60 to 3600, default 600
// optionFields lists resolution as a choice and wait_seconds as a whole number. checkAddress is the status of
// parseBlenderKitAddress. storeSourceKinds is kSourceBlenderKit.
// There are no per import options.
struct BlenderKitOptions {
    std::string resolution = "2k";
    int waitSeconds = 600;
};

// Reads the options. InvalidValue naming the option for an unknown key or a bad value.
Status blenderKitOptionsFromJson(const Json& json, BlenderKitOptions& out);

// The fetch for one item: task kBlenderKitFetchTask, params as above without output_folder, the expected kind
// (kFetchKindModel or kFetchKindMaterial), timeout wait_seconds plus 120.
ImportFetch makeBlenderKitFetch(std::string_view assetBaseId, std::string_view kind,
                                const BlenderKitOptions& options);

}
