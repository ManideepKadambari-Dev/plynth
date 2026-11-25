"""The rules of the BlenderKit fetch (BlenderKit.h, the twelve steps) checked without Blender: the real worker
and guard run in the stand-in Blender with the fake add-on and the fake helper. Each test names its finding.
Run: python -I blender/tests/plain/run_plain_tests.py, with PLYNTH_FAKE_STORE_HELPER set by the build.
"""
import json
import os
import shutil
import subprocess
import time
import unittest

import fetch_rig
from fetch_rig import ADDON, KEY, MATERIAL, MODEL, OTHER, REFRESH, Item, Rig

ADDON_MISSING = "Open Blender once with the BlenderKit add-on enabled, then try again."
SECRETS = (KEY, REFRESH, "SECRET-ADDRESS", "SECRET-MAIL")


class FetchCase(unittest.TestCase):
    # A rig for each test.
    def setUp(self):
        self.rig = Rig(self)

    # Asserts a good fetch and returns its result.
    def assert_ok(self, result):
        self.assertIsNotNone(result, self.rig.output())
        self.assertTrue(result.get("ok"), json.dumps(result.get("error"))[:500] + self.rig.output())
        return result

    # Asserts a failed fetch with one of the codes.
    def assert_failed(self, result, *codes):
        self.assertIsNotNone(result, self.rig.output())
        self.assertFalse(result.get("ok"), self.rig.output())
        self.assertIn(result["error"]["code"], codes, self.rig.output())
        return result

    # Asserts that no download request reached the helper.
    def assert_no_request(self):
        self.assertEqual(self.rig.requests("asset_download"), [], self.rig.output())

    # The data of the one download request.
    def only_request(self):
        sent = self.rig.requests("asset_download")
        self.assertEqual(len(sent), 1, self.rig.output())
        return sent[0]["data"]

    # Whether a path is inside the fetch folder.
    def inside(self, path):
        folder = os.path.normcase(os.path.realpath(self.rig.private))
        return os.path.normcase(os.path.realpath(path)).startswith(folder + os.sep)


    # Starts a fake helper as the user's own, on the add-on's first standard port, from the user's data folder.
    def start_user_helper(self):
        exe = os.path.join(self.rig.data, "client", "bin", "bk_client.exe")
        shutil.copyfile(fetch_rig.helper_program(), exe)
        log = os.path.join(self.rig.root, "user_helper.log")
        scenario = os.path.join(self.rig.root, "user_helper.json")
        self.rig.write(scenario, json.dumps({"linger_s": 3600}))
        env = dict(self.rig.environment(), FAKE_STORE_LOG=log, FAKE_STORE_SCENARIO=scenario)
        port = self.rig.addon_scenario["standard_ports"][0]
        process = subprocess.Popen([exe, "--port", port, "--global_dir", self.rig.data], env=env,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        self.addCleanup(process.wait)
        self.addCleanup(process.kill)
        for _ in range(50):
            if fetch_rig.read_lines(log):
                break
            time.sleep(0.1)
        return log

    # What reached the user's helper after its start.
    def user_helper_requests(self, log):
        return [entry for entry in fetch_rig.read_lines(log) if entry.get("op") not in ("started", "wrote")]


class GoodFetches(FetchCase):
    # W1, W15, B2: a model lands from a fresh private download. The user's own copy is unchanged and not used.
    def test_model_from_a_fresh_private_download(self):
        before = self.rig.fingerprint()
        result = self.assert_ok(self.rig.fetch(MODEL))
        output = result["output"]
        self.assertEqual(output["kind"], "model")
        self.assertEqual(output["reference"], "https://www.blendkit.com/asset-gallery-detail/%s/" % MODEL.base_id)
        with open(os.path.join(self.rig.private, "out", output["file"]), "r", encoding="utf-8") as handle:
            self.assertIn("Item A", handle.read())
        self.assertEqual(self.rig.fingerprint(), before)
        data = self.only_request()
        self.assertTrue(all(self.inside(folder) for folder in data["download_dirs"]), data["download_dirs"])
        self.assertEqual(self.rig.events("append_ran"), [])
        self.assertEqual(self.rig.events("downloaded_file_deleted"), [])

    # W1, W14: a material by its trimmed name among several, with a throwaway target and nothing deleted.
    def test_material_by_trimmed_name(self):
        self.rig.helper_scenario["search"] = [MATERIAL.record(name="  Oak floor ", displayName="Oak Floor Deluxe")]
        self.rig.write(self.rig.source, json.dumps({"fake_blend": 1, "materials": ["Walnut", "Oak floor"]}))
        before = self.rig.fingerprint()
        result = self.assert_ok(self.rig.fetch(MATERIAL))
        self.assertEqual(result["output"]["material"], "Oak floor")
        self.assertEqual(self.rig.fingerprint(), before)
        calls = [entry for entry in self.rig.events("operator") if entry["idname"] == "scene.blenderkit_download"]
        self.assertEqual(len(calls), 1)
        self.assertTrue(calls[0]["options"].get("target_object"), calls[0])

    # W14: several materials and none with the item's name: failed.
    def test_material_without_its_name_fails(self):
        self.rig.helper_scenario["search"] = [MATERIAL.record()]
        self.rig.write(self.rig.source, json.dumps({"fake_blend": 1, "materials": ["Walnut", "Pine"]}))
        self.assert_failed(self.rig.fetch(MATERIAL), "download_failed")

    # W5: each resolution reaches the request through the preference, under the server's name the add-on
    # writes into the request and its copy of the preferences; the saved one is unchanged.
    def test_each_resolution_reaches_the_request(self):
        saved = os.path.join(self.rig.blender_user, "config", "userpref.json")
        for resolution, value in fetch_rig.RESOLUTION_SERVER.items():
            with self.subTest(resolution=resolution):
                self.assert_ok(self.rig.fetch(MODEL, resolution))
                data = self.only_request()
                self.assertEqual(data["PREFS"]["resolution"], value)
                self.assertEqual(data["resolution"], value)
                self.assertEqual(self.rig.events("operator_resolution"), [])
                with open(saved, "r", encoding="utf-8") as handle:
                    self.assertEqual(json.load(handle)["addons"]["blenderkit"]["resolution"], "1024")

    # D10, W13, D9: the request's preferences and the helper's home, app data and temp folders are private.
    def test_preferences_copy_and_child_folders_are_private(self):
        self.assert_ok(self.rig.fetch(MODEL))
        data = self.only_request()
        self.assertTrue(self.inside(data["PREFS"]["global_dir"] + "/x"), data["PREFS"]["global_dir"])
        self.assertEqual(data["PREFS"]["directory_behaviour"], "GLOBAL")
        self.assertFalse(data["PREFS"]["unpack_files"])
        self.assertFalse(data["PREFS"]["create_asset_library"])
        started = self.rig.requests("started")
        self.assertTrue(started)
        for name, value in started[0]["env"].items():
            with self.subTest(variable=name):
                self.assertTrue(value == "" or self.inside(os.path.join(value, "x")), value)

    # Search timeout: the operator's search waits longer than the add-on's 1 second.
    def test_slow_search_still_lands(self):
        self.rig.helper_scenario["search_delay_ms"] = 3000
        # The search blocks the add-on's timer, so no report reaches the helper while it waits. The helper must
        # outlive that wait, or it would end in the middle of the search for a reason this test does not ask about.
        self.rig.helper_scenario["linger_s"] = 10
        self.assert_ok(self.rig.fetch(MODEL))

    # W6, B1: no secret of the task, the record or the add-on's own log reaches the result or the output.
    def test_no_secret_in_result_or_output(self):
        result = self.assert_ok(self.rig.fetch(MODEL))
        text = json.dumps(result)
        for current, _folders, names in os.walk(os.path.join(self.rig.private, "out")):
            for name in names:
                with open(os.path.join(current, name), "rb") as handle:
                    text += handle.read().decode("utf-8", "replace")
        for secret in SECRETS:
            self.assertNotIn(secret, text)

    # W7, D3: no queued save, background update, settings, categories or other task runs.
    def test_no_queue_timer_or_other_task_runs(self):
        self.rig.helper_scenario["first_tasks"] = [
            {"task_type": "settings", "send_usage_data": False}, {"task_type": "categories"},
            {"task_type": "login", "api_key": "NEW-KEY-MARKER", "refresh_token": "NEW-TOKEN-MARKER"}]
        before = self.rig.fingerprint()
        self.assert_ok(self.rig.fetch(MODEL))
        for event in ("queued_task_ran", "bg_update_ran", "prefs_file_written", "settings_applied",
                      "categories_written", "write_tokens", "task_other", "save_prefs"):
            self.assertEqual(self.rig.events(event), [], event)
        self.assertEqual(self.rig.fingerprint(), before)


class GateRefusals(FetchCase):
    # W11, D2: the search answers another item.
    def test_another_base_id(self):
        self.rig.helper_scenario["search"] = [OTHER.record()]
        self.assert_failed(self.rig.fetch(MODEL), "download_failed")
        self.assert_no_request()

    # W12: the job holds a version id, as a page address can, and the search answers the item.
    def test_version_id_in_the_job(self):
        asked = Item("a")
        asked.base_id = MODEL.version_id
        self.rig.helper_scenario["search"] = [MODEL.record()]
        self.assert_failed(self.rig.fetch(asked), "download_failed")
        self.assert_no_request()

    # D2: another kind than asked, and a version id that is no uuid.
    def test_other_kind_and_bad_version_id(self):
        for changes in ({"assetType": "material"}, {"id": "not-a-uuid"}):
            with self.subTest(changes=changes):
                self.rig.helper_scenario["search"] = [MODEL.record(**changes)]
                self.assert_failed(self.rig.fetch(MODEL), "download_failed", "wrong_asset_kind")
                self.assert_no_request()

    # D2: a download folder outside, unpack on, another resolution in either of its names, in the request or
    # only in its preferences, or no resolution in the request, each set by the add-on after the worker. The
    # sentence and the refusal line name the fixed word of the check.
    def test_hostile_request_fields(self):
        outside = os.path.join(self.rig.root, "outside")
        cases = (({"extra_download_dir": outside}, "folders"), ({"force_unpack": True}, "unpack"),
                 ({"force_resolution": "8192"}, "resolution"), ({"force_resolution": "resolution_8K"}, "resolution"),
                 ({"force_pref_resolution": "8192"}, "resolution"),
                 ({"force_pref_resolution": "resolution_8K"}, "resolution"),
                 ({"drop_resolution": True}, "resolution"))
        for hostile, word in cases:
            with self.subTest(hostile=hostile):
                self.rig.addon_scenario = dict(self.rig.addon_scenario, **hostile)
                result = self.assert_failed(self.rig.fetch(MODEL), "download_failed")
                self.assertIn("(%s)" % word, result["error"]["message"])
                self.assertIn("PLYNTH-REFUSAL:%s\n" % word, self.rig.done.stdout)
                self.assert_no_request()
                self.assertFalse(os.path.exists(outside))
                for name in hostile:
                    self.rig.addon_scenario.pop(name)

    # D2: both names of the asked resolution pass, in the request and in its preferences.
    def test_both_names_of_the_asked_resolution(self):
        for forced in ("2048", "resolution_2K"):
            with self.subTest(forced=forced):
                self.rig.addon_scenario = dict(self.rig.addon_scenario, force_resolution=forced)
                self.assert_ok(self.rig.fetch(MODEL, "2k"))
                self.assertEqual(self.only_request()["resolution"], forced)
                self.rig.addon_scenario.pop("force_resolution")

    # D1: a file list as the store sends it keeps the .blend and its resolution files, in their order, and
    # drops thumbnails, zips, glTF and proxy files.
    def test_store_file_list(self):
        record = MODEL.record()
        address = "https://www.blendkit.com/api/v1/downloads/%s/"
        record["files"] = [{"fileType": name, "downloadUrl": address % name} for name in
                           ("thumbnail", "blend", "resolution_0_5K", "resolution_1K", "resolution_2K", "zip_file",
                            "gltf", "prxc", "photo_thumbnail")]
        self.rig.helper_scenario["search"] = [record]
        self.assert_ok(self.rig.fetch(MODEL))
        sent = self.only_request()
        self.assertEqual([entry["fileType"] for entry in sent["asset_data"]["files"]],
                         ["blend", "resolution_0_5K", "resolution_1K", "resolution_2K"])

    # D1: zip files are taken out of the request; only zips is a refusal; other hosts are refused.
    def test_zip_files_and_hosts(self):
        zipped = {"fileType": "zip_file", "downloadUrl": "https://www.blendkit.com/api/v1/downloads/z/"}
        record = MODEL.record()
        record["files"].append(zipped)
        self.rig.helper_scenario["search"] = [record]
        self.rig.helper_scenario["download"]["unzip"] = [{"name": "../../escaped.txt"}, {"name": "inside.txt"}]
        self.assert_ok(self.rig.fetch(MODEL))
        sent = self.only_request()
        self.assertEqual([entry["fileType"] for entry in sent["asset_data"]["files"]], ["blend"])
        self.assertEqual(self.rig.requests("wrote"), [])
        bad_files = ([zipped],
                     [{"fileType": "blend", "downloadUrl": "http://www.blendkit.com/api/v1/downloads/x/"}],
                     [{"fileType": "blend", "downloadUrl": "https://www.blendkit.com.lookalike.invalid/x/"}],
                     [{"fileType": "blend", "downloadUrl": "https://notblendkit.invalid/x/"}])
        for files in bad_files:
            with self.subTest(files=files):
                self.rig.helper_scenario["search"] = [MODEL.record(files=files)]
                self.assert_failed(self.rig.fetch(MODEL), "download_failed")
                self.assert_no_request()

    # W2, the lookup: an add-on item, under the asked id or another, is refused at the search of the operator,
    # before it branches. No request, the add-on route never runs and leaves no marker.
    def test_addon_item(self):
        for record in (ADDON.record(assetBaseId=MODEL.base_id), ADDON.record()):
            with self.subTest(base_id=record["assetBaseId"]):
                self.rig.helper_scenario["search"] = [record]
                self.assert_failed(self.rig.fetch(MODEL), "wrong_asset_kind", "download_failed")
                self.assert_no_request()
                self.assertEqual(self.rig.events("addon_choice_ran"), [])
                self.assertFalse(os.path.exists(self.rig.marker("addon_choice")))
                reached = {entry["idname"] for entry in self.rig.events("operator")}
                self.assertNotIn("scene.blenderkit_addon_choice", reached)
                self.assertNotIn("extensions.package_install_files", reached)

    # Whether the last fetch reached the add-on's download operator.
    def operator_reached(self):
        return any(entry.get("idname") == "scene.blenderkit_download" for entry in self.rig.events("operator"))

    # Whether the last fetch lost its helper before the operator: it never answered, or it ended at once,
    # for example because a test running beside this one took its port first. Nothing of the seal was tried.
    def helper_lost_before_operator(self):
        if self.operator_reached():
            return False
        started = self.rig.events("client_started")
        return len(started) != 1 or not self.rig.requests("report")

    # Runs a fetch whose add-on tries one group of hostile calls inside the operator, so after the seal. Each
    # call is refused, none of them ran, and nothing in the scratch home changed. A fetch that lost its helper
    # before the operator, which has nothing to do with the seal, is run again, at most three times in all.
    def assert_group_closed(self, group, names):
        self.rig.addon_scenario["hostile_calls"] = [group]
        before = self.rig.fingerprint()
        for _ in range(3):
            result = self.rig.fetch(MODEL)
            if not self.helper_lost_before_operator():
                break
        error = json.dumps((result or {}).get("error"))[:500]
        self.assertTrue(self.operator_reached(), "the fetch ended before the operator: " + error + self.rig.output())
        self.assertEqual(self.rig.events("hostile_call_ran"), [], self.rig.output())
        refused = sorted(entry["name"] for entry in self.rig.events("hostile_call_refused"))
        self.assertEqual(refused, sorted(names), error + self.rig.output())
        self.assertEqual(self.rig.events("bundled_ran"), [], self.rig.output())
        self.assertEqual(self.rig.events("addon_choice_ran"), [])
        self.assertFalse(os.path.exists(self.rig.marker("addon_choice")))
        self.assertEqual([entry for entry in self.rig.events("addon_enable") if entry["module"] != "blenderkit"], [])
        self.assertEqual(self.rig.events("addon_disable"), [])
        self.assertEqual(self.rig.fingerprint(), before)

    # W2, Python operators: each named operator called by its name after the seal is gone.
    def test_named_operators_after_the_seal(self):
        self.assert_group_closed("named", ("addon_enable", "addon_disable", "addon_install", "addon_remove",
                                           "package_install_files", "package_install", "package_uninstall",
                                           "addon_choice"))

    # W2: a callable the add-on took when it registered, before the seal, fails once its class is gone.
    def test_callable_taken_before_the_seal(self):
        self.assert_group_closed("early", ("addon_enable", "package_install_files", "addon_choice"))

    # W2: addon_utils.enable and disable, read through their module, are refusals after the seal.
    def test_addon_utils_after_the_seal(self):
        self.assert_group_closed("addon_utils", ("enable", "disable"))

    # W2, D3, references taken before the seal: a second add-on took addon_utils.enable and disable and the
    # logout of the add-on by name when it was imported, and calls them inside the lookup, after the seal. Each
    # is a refusal: nothing is enabled, disabled or signed out, no logout request leaves, and the fetch lands.
    def test_references_taken_before_the_seal(self):
        self.rig.add_held_references()
        before = self.rig.fingerprint()
        self.assert_ok(self.rig.fetch(MODEL))
        self.assertEqual(self.rig.events("held_call_ran"), [], self.rig.output())
        refused = sorted({entry["name"] for entry in self.rig.events("held_call_refused")})
        self.assertEqual(refused, ["disable", "enable", "logout"], self.rig.output())
        enabled = [entry for entry in self.rig.events("addon_enable")
                   if entry["module"] not in ("blenderkit", "held_refs")]
        self.assertEqual(enabled, [])
        self.assertEqual(self.rig.events("addon_disable"), [])
        for event in ("logout", "oauth2_logout", "clean_login_data"):
            self.assertEqual(self.rig.events(event), [], event)
        self.assertEqual(self.rig.requests("oauth2_logout"), [])
        self.assertEqual(self.rig.fingerprint(), before)

    # W13: an add-on whose helper folder stays outside the fetch folder fails before the operator.
    def test_helper_folder_outside(self):
        self.rig.addon_scenario["client_dir"] = os.path.join(self.rig.data, "client")
        self.assert_failed(self.rig.fetch(MODEL), "download_failed")
        self.assert_no_request()
        self.assertEqual(self.rig.events("client_started"), [])

    # Step 2: a helper that runs from the add-on's own folder is refused.
    def test_helper_in_place(self):
        self.rig.addon_scenario["inplace"] = True
        self.assert_failed(self.rig.fetch(MODEL), "download_failed")
        self.assertEqual(self.rig.events("client_started"), [])


class SignIn(FetchCase):
    # W3, D5: the saved file with keep_preferences false, missing, broken and true. Unchanged in all four,
    # signed in only for true. The user's own helper on a standard port gets no post.
    def test_saved_sign_in_file(self):
        saved = os.path.join(self.rig.data, "config", "preferences.json")
        cases = {"false": json.dumps({"api_key": KEY, "keep_preferences": False, "send_usage_data": False}),
                 "missing": None, "broken": "{not json",
                 "true": json.dumps({"api_key": KEY, "keep_preferences": True, "send_usage_data": False})}
        user_helper = self.start_user_helper()
        for name, text in cases.items():
            with self.subTest(case=name):
                if text is None and os.path.exists(saved):
                    os.remove(saved)
                elif text is not None:
                    self.rig.write(saved, text)
                self.rig.prefs.update(api_key="", api_key_refresh="", api_key_timeout=0)
                before = self.rig.fingerprint()
                result = self.rig.fetch(MODEL)
                if name == "true":
                    self.assert_ok(result)
                else:
                    self.assert_failed(result, "not_signed_in")
                    self.assert_no_request()
                self.assertEqual(self.rig.fingerprint(), before)
                self.assertEqual(self.rig.events("settings_posted"), [])
                self.assertEqual(self.rig.events("prefs_file_deleted"), [])
        self.assertEqual(self.user_helper_requests(user_helper), [])

    # D6: a typed key (timeout 0, no refresh token) and an old timeout without a refresh token are signed in.
    # A refresh token with a timeout that passed is not, and nothing is refreshed.
    def test_typed_key_and_timeouts(self):
        cases = ((0, "", True), (int(time.time()) - 86400, "", True), (int(time.time()) - 86400, REFRESH, False))
        for timeout, refresh, signed_in in cases:
            with self.subTest(timeout=timeout, refresh=bool(refresh)):
                self.rig.prefs.update(api_key=KEY, api_key_refresh=refresh, api_key_timeout=timeout)
                result = self.rig.fetch(MODEL)
                if signed_in:
                    self.assert_ok(result)
                else:
                    self.assert_failed(result, "not_signed_in")
                    self.assert_no_request()
                self.assertEqual(self.rig.requests("refresh_token"), [])

    # D3, W8: an "Invalid token." error and a logout task in the batch with the download: no refresh, no logout,
    # no revoke request, the key unchanged, and the download still lands.
    def test_invalid_token_and_logout_task(self):
        self.rig.helper_scenario["first_tasks"] = [
            {"task_type": "search", "status": "error", "message": "Invalid token."},
            {"task_type": "oauth2_logout", "api_key": KEY, "refresh_token": REFRESH}]
        before = self.rig.fingerprint()
        self.assert_ok(self.rig.fetch(MODEL))
        for op in ("refresh_token", "oauth2_logout"):
            self.assertEqual(self.rig.requests(op), [], op)
        for event in ("refresh_token", "oauth2_logout", "logout", "clean_login_data", "write_tokens"):
            self.assertEqual(self.rig.events(event), [], event)
        self.assertEqual(self.rig.fingerprint(), before)


class Ports(FetchCase):
    # D4: the user's own helper on a standard port is never connected to, and the fetch's helper runs on a
    # high port that is none of the eight.
    def test_users_helper_never_met(self):
        log = self.start_user_helper()
        before = self.rig.fingerprint()
        self.assert_ok(self.rig.fetch(MODEL))
        self.assertEqual(self.user_helper_requests(log), [])
        ports = {entry["port"] for entry in self.rig.requests("started")}
        self.assertEqual(len(ports), 1)
        port = ports.pop()
        self.assertGreater(int(port), 49151)
        self.assertNotIn(port, self.rig.addon_scenario["standard_ports"])
        self.assertEqual(self.rig.fingerprint(), before)

    # W16, step 9: an add-on that keeps talking on the standard ports meets the user's helper, which this
    # Blender did not start: refused, and no download reaches it.
    def test_helper_not_started_here_is_refused(self):
        log = self.start_user_helper()
        self.rig.addon_scenario["ignore_port_change"] = True
        self.assert_failed(self.rig.fetch(MODEL), "download_failed")
        self.assertEqual([entry for entry in fetch_rig.read_lines(log) if entry.get("op") == "asset_download"], [])


class HelperEndings(FetchCase):
    # W4: a helper that stops reporting the task after writing the file: dropped after 20 seconds, failed,
    # although a .blend lies in the item folder.
    def test_silent_task(self):
        self.rig.helper_scenario["download"]["silent"] = True
        self.assert_failed(self.rig.fetch(MODEL), "download_failed")
        self.assertTrue(self.rig.requests("delivered"))

    # W4: a finished task that names a file that is not there: failed.
    def test_noted_file_missing(self):
        self.rig.helper_scenario["download"]["fake_path"] = os.path.join(self.rig.private, "models", "gone.blend")
        self.assert_failed(self.rig.fetch(MODEL), "download_failed")

    # W4, step 11: a finished task naming a .blend outside the item's folder, here the user's own copy: failed.
    def test_noted_file_outside(self):
        self.rig.helper_scenario["download"]["fake_path"] = self.rig.user_item
        self.assert_failed(self.rig.fetch(MODEL), "download_failed")

    # D11: no download is sent before the helper's first report, and a restarted helper ends the fetch.
    def test_early_download_and_restart(self):
        self.assert_ok(self.rig.fetch(MODEL))
        self.assertEqual(self.rig.requests("crashed"), [])
        self.rig.helper_scenario["die_after_reports"] = 2
        self.rig.helper_scenario["download"]["stall_s"] = 5
        self.assert_failed(self.rig.fetch(MODEL), "download_failed")

    # A download that never ends: download_timeout after wait_seconds, cancelled through the add-on.
    def test_timeout_cancels(self):
        self.rig.helper_scenario["download"]["never"] = True
        self.assert_failed(self.rig.fetch(MODEL, wait=60), "download_timeout")
        self.assertTrue(self.rig.requests("cancel"))

    # The helper's "Invalid token." error for the download itself: failed, and nothing refreshed.
    def test_download_error(self):
        self.rig.helper_scenario["download"]["error"] = "Invalid token."
        self.assert_failed(self.rig.fetch(MODEL), "download_failed")
        self.assertEqual(self.rig.requests("refresh_token"), [])


class FetchSetup(FetchCase):
    # Step 1: without the guard the task refuses before anything starts.
    def test_without_guard(self):
        self.assert_failed(self.rig.fetch(MODEL, guard=False), "download_failed", "bad_job")
        self.assertEqual(self.rig.events("client_started"), [])
        self.assertEqual(self.rig.requests(), [])

    # Step 1: in a private folder that is not labelled low integrity, the low integrity fetch can do nothing.
    # Windows refuses every write there, so no result can be written: no helper starts or is asked anything,
    # the folder holds only what the test put there, and the home is unchanged.
    def test_folder_not_labelled(self):
        self.rig = Rig(self, low=False)
        before = self.rig.fingerprint()
        self.assertIsNone(self.rig.fetch(MODEL), self.rig.output())
        self.assertEqual(self.rig.requests(), [], self.rig.output())
        left = sorted(os.path.relpath(os.path.join(current, name), self.rig.private).replace("\\", "/")
                      for current, folders, names in os.walk(self.rig.private) for name in folders + names)
        self.assertEqual(left, ["job.json", "tmp"], self.rig.output())
        self.assertEqual(self.rig.fingerprint(), before)

    # D7: an add-on that cannot make its updater folder: addon_missing with its sentence, nothing made.
    def test_updater_folder_missing(self):
        shutil.rmtree(os.path.join(self.rig.addon, "blenderkit_updater"))
        before = self.rig.fingerprint()
        result = self.assert_failed(self.rig.fetch(MODEL), "addon_missing")
        self.assertIn(ADDON_MISSING, result["error"]["message"])
        self.assertFalse(os.path.exists(os.path.join(self.rig.addon, "blenderkit_updater")))
        self.assertEqual(self.rig.fingerprint(), before)

    # The seal: a Blender version outside the tested list is refused before the add-on is used. No helper
    # starts, the operator never runs, no request leaves.
    def test_untested_blender_version(self):
        for version in ("5.3.0", "4.2.0", "6.0.0"):
            with self.subTest(version=version):
                self.rig.extra_env["FAKE_BLENDER_VERSION"] = version
                self.assert_failed(self.rig.fetch(MODEL), "download_failed")
                self.assert_no_request()
                self.assertEqual(self.rig.events("client_started"), [])
                calls = [entry for entry in self.rig.events("operator")
                         if entry["idname"] == "scene.blenderkit_download"]
                self.assertEqual(calls, [])

    # The seal proves itself: when one named class stays registered, the fetch fails closed before the helper
    # starts and before the operator runs.
    def test_seal_fails_closed_when_a_class_stays(self):
        for name in ("SCENE_OT_blenderkit_addon_choice", "PREFERENCES_OT_addon_enable",
                     "EXTENSIONS_OT_package_install_files", "EXTENSIONS_OT_package_upgrade_all"):
            with self.subTest(kept=name):
                self.rig.extra_env["FAKE_BPY_KEEP_CLASS"] = name
                self.assert_failed(self.rig.fetch(MODEL), "download_failed")
                self.assert_no_request()
                self.assertEqual(self.rig.events("client_started"), [])
                calls = [entry for entry in self.rig.events("operator")
                         if entry["idname"] == "scene.blenderkit_download"]
                self.assertEqual(calls, [])
                self.assertFalse(os.path.exists(self.rig.marker("addon_choice")))

    # Step 2: no bytecode is written in the user's add-on folder.
    def test_no_bytecode_in_the_addon(self):
        self.assert_ok(self.rig.fetch(MODEL))
        self.assertFalse(os.path.exists(os.path.join(self.rig.addon, "__pycache__")))


if __name__ == "__main__":
    unittest.main()
