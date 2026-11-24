#pragma once

#include "../Status.h"
#include "ImportFetch.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace plynth {

// BlenderKit import. The user pastes an item's address; core reads
// the item's id from it, and Blender downloads the item through the BlenderKit add-on the user installed and
// signed in to inside Blender. Plynth never calls a BlenderKit web address itself, never writes the add-on's
// key anywhere, and never shows or stores what Blender or the add-on prints during a download. Every fetch
// downloads afresh into Plynth's own fetch folder (StoreFetch.h), at low integrity: the user's BlenderKit
// folder is never a download target and nothing is copied out of it. The add-on's helper program does
// connect to BlenderKit with the user's key, as it does when the user works in Blender, and shows its tray
// icon while it runs.
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
inline constexpr std::string_view kBlenderKitSite = "https://www.blendkit.com/";

// The worker task (blender/plynth/tasks/blenderkit_fetch.py).
// Params: {"asset_base_id": "0c1d4a2e-....", "kind": "model" or "material", "resolution": "2k",
//          "output_folder": "C:/Users/Ann/AppData/Local/Plynth/fetch/out", "wait_seconds": 600,
//          "private_folder": "C:/Users/Ann/AppData/Local/Plynth/fetch"}
// The last one and output_folder are added by runImportFetch. Both are the fetch folder of StoreFetch.h and
// a folder in it.
// Steps, which makeBlenderKitFetch lists with their sentences: "check_addon" "Checking the BlenderKit add-on",
// "download" "Downloading from BlenderKit", "copy" "Copying the download".
//
// The order inside the task. Each rule fails closed: when it cannot be met, or a part of the add-on it needs
// is not there, the task fails before the operator runs, or before it runs the add-on's timer again.
// Names are those of add-on 3.21.7.261119 (notes of the source check). A name that is missing fails closed.
//  1. Without the guard of StoreFetch.h, or when private_folder is not the low integrity fetch folder, the
//     task refuses. platform.platform() is called once before the guard's hook is in, so no later call needs
//     to start a program. The guard allows reading the registry and opening NUL. When the add-on could not
//     register because a write was refused (its updater folder is made on its first start), the task fails
//     with addon_missing and "Open Blender once with the BlenderKit add-on enabled, then try again."
//  2. Seal: from then on the only operator that runs is scene.blenderkit_download, add-ons can be neither
//     enabled nor disabled, and the home, app data and XDG variables that child programs get point into
//     private_folder. client_lib.is_using_inplace_client() must be false.
//  3. Ports: global_vars.CLIENT_PORTS becomes three free ports above 49151, none of the add-on's eight,
//     before anything can talk to a helper. So this Blender never meets the user's own helper, and a Blender
//     the user opens meanwhile never meets this one.
//  4. Sign in, read once: whether api_key and api_key_refresh are set, and api_key_timeout. The loader of the
//     saved JSON runs only when no key is held and that file is a JSON object with keep_preferences true, with
//     client_lib.applying_client_settings true around it. A missing key is not_signed_in. Expired is only a
//     refresh token that is set with a timeout above 0 that has passed: a key typed by hand (no refresh token)
//     never expires here. Then api_key_refresh is blanked in memory.
//  5. Replaced in memory, each with a function that does nothing and raises nothing: refreshing
//     (client_lib.refresh_token, bkit_oauth.ensure_token_refresh), ending (bkit_oauth.logout,
//     client_lib.oauth2_logout, bkit_oauth.clean_login_data), storing tokens (bkit_oauth.write_tokens).
//     timer.handle_task passes only download tasks to download.handle_download_task and drops every other
//     task. So no sign in is refreshed, revoked or written, and no settings, categories or popups are handled.
//  6. In memory, preferences_lock first (again after the loader, which unlocks): global_dir =
//     private_folder, directory_behaviour "GLOBAL", unpack_files and create_asset_library off,
//     use_clipboard_scan and thread_communication off, resolution as asked. keep_preferences,
//     send_usage_data, api_key, the port and the proxy fields are never set. Then the worker makes the copy
//     itself: global_vars.PREFS = utils.get_preferences_as_dict(). paths.dirs_exist_dict is emptied, and
//     every folder the add-on and helper would use (paths.get_download_dirs, paths.get_config_dir_path,
//     client_lib.get_client_directory, client_lib.get_client_log_path, tempfile.gettempdir()) must be inside
//     private_folder. paths.get_temp_dir is never called.
//  7. The append step: download.try_finished_append is replaced by one that reads only kwargs["file_paths"]
//     [-1] and the named fields of asset_data, notes them for this base id, and changes nothing. A call
//     without file_paths is refused. Records of used items in the startup scene are cleared in memory.
//  8. The request gate: client_lib.asset_download is wrapped. Before it calls the real one it checks, and
//     raises otherwise, so no request leaves and no task exists: the base id is the asked one (a page address
//     can hold a version id, and the server's search can answer another item), the kind is the asked one
//     and is model or material, the version id is a uuid, the download folders are inside private_folder,
//     both unpack flags are off, the resolution is the asked one. Its files list keeps only .blend files of
//     that kind; zip files are taken out, and none left is a refusal. Every download address is https on
//     blendkit.com or blenderkit.com or a name under them. client_lib.blocking_request is wrapped too, so the
//     operator's search waits up to 15 seconds instead of 1.
//  9. Only timer.client_communication_timer is run, never the task queue or other timers. The operator runs
//     only once global_vars.CLIENT_ACCESSIBLE is true and global_vars.client_process is this Blender's living
//     child (client_lib.is_client_process_alive()). The helper sends a download before its first report
//     only to crash, and one that was restarted ends the fetch as download_failed.
// 10. The operator gets the id, and for a material a throwaway mesh as its target, with no object active.
//     An add-on item can only reach refused operators and is refused at the gate first.
// 11. Success is only a noted path for this base id that exists inside private_folder, in this item's folder
//     under the folder of its kind, ending .blend. Everything else (an empty task list, a write time, a file
//     name, being the only file) is never evidence. download.cancel_running_downloads ends a timeout.
// 12. Copied to output_folder: that .blend and the textures folders beside it. Of the task and asset_data
//     only named fields are read (base id, version id, kind, name, author's full name, license), never the
//     whole, which holds secrets and addresses. There is no record of the item, and the scene is never saved.
//     The material is the one whose name is the item's name, trimmed, or the only one; otherwise the fetch
//     fails.
//
// Output: FetchOutput of StoreFetch.h with format "blend", reference
// "https://www.blendkit.com/asset-gallery-detail/<base id>/".
// Worker error codes, each with a fixed sentence (StoreFetch.h): addon_missing (not installed or not enabled),
// not_signed_in (no key, or a sign in that has expired: sign in inside Blender), download_failed (the add-on
// refused or is not one these rules work with, the helper could not start or was restarted, the gate refused,
// the task ended without a file, or a rule above could not be met), download_timeout (not finished after
// wait_seconds, the downloads are then cancelled through the add-on), wrong_asset_kind (a model or a material gets the sentence of
// applyFetch naming the right tab; HDR, scene, brush, add-on and other kinds Plynth knows by name are named,
// any other or unknown kind gets a sentence without the add-on's word, and none is ever taken as a model).
inline constexpr std::string_view kBlenderKitFetchTask = "blenderkit_fetch";

// Texture resolutions the add-on can download. "original" takes the files as the author uploaded them.
// 0.5k 1k 2k 4k 8k original
bool isBlenderKitResolution(std::string_view resolution);

struct BlenderKitAddress {
    Status status;           // InvalidValue with a plain sentence when the text is not a BlenderKit address
    std::string assetBaseId; // lower case, 8-4-4-4-12 hex digits
};

// Reads the item id from what the user pasted, after trimming spaces:
// - an https or http address on blendkit.com, www.blendkit.com, blenderkit.com or www.blenderkit.com whose
//   path holds one id (8-4-4-4-12 hex digits), for example https://www.blendkit.com/get-blendkit/<id>/ or
//   .../asset-gallery-detail/<id>/. Whether it is a base id cannot be seen here, the fetch compares it.
// - the text "asset_base_id:<id>" as the add-on's own search box takes it, also as the add-on copies it,
//   "asset_base_id:<id> asset_type:<type>" with other text allowed between the two. The two words may be in
//   any letter case. The type model or material is only a hint, the fetch checks the kind itself.
// - a bare id
// Refusals: another site "This is not a BlenderKit address."; no id or two different ids "The address does not
// name one BlenderKit item. Copy it from the item's page."; a copied type other than model or material "Only
// models and materials can be imported from BlenderKit." (so no Blender starts for an add-on item).
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

// The fetch for one item: task kBlenderKitFetchTask, params as above without output_folder and private_folder,
// the expected kind (kFetchKindModel or kFetchKindMaterial), timeout wait_seconds plus 120, and the three
// steps.
ImportFetch makeBlenderKitFetch(std::string_view assetBaseId, std::string_view kind,
                                const BlenderKitOptions& options);

}
