import json
import os

RESULT_FORMAT = "plynth-unreal-result"
RESULT_VERSION = 1


class Result:
    # Makes an empty result for a job id.
    def __init__(self, job_id=""):
        self.job_id = job_id
        self.error = None
        self.engine_version = ""
        self.project_name = ""
        self.content_path = ""
        self.assets = []
        self.actors = []
        self.checks = []
        self.screenshot = None
        self.ms = 0

    # Records what happened to one asset. A later record of the same path replaces the earlier one.
    def asset(self, path, kind, action):
        for entry in self.assets:
            if entry["path"] == path:
                entry["kind"] = kind
                entry["action"] = action
                return
        self.assets.append({"path": path, "kind": kind, "action": action})

    # Returns the action recorded for an asset path, or None.
    def asset_action(self, path):
        for entry in self.assets:
            if entry["path"] == path:
                return entry["action"]
        return None

    # Records what happened to one actor.
    def actor(self, label, asset, action):
        self.actors.append({"label": label, "asset": asset, "action": action})

    # Records one self check.
    def check(self, check_id, status, message):
        self.checks.append({"id": check_id, "status": status, "message": message})

    # Records the error that ended the job.
    def fail(self, code, message):
        self.error = {"code": code, "message": message}

    # Returns the result in the form of schema/unreal-result.schema.json.
    def to_json(self):
        return {
            "format": RESULT_FORMAT,
            "version": RESULT_VERSION,
            "job_id": self.job_id,
            "ok": self.error is None,
            "error": self.error,
            "engine_version": self.engine_version,
            "project_name": self.project_name,
            "content_path": self.content_path,
            "assets": self.assets,
            "actors": self.actors,
            "checks": self.checks,
            "screenshot": self.screenshot,
            "ms": max(0, int(self.ms)),
        }

    # Writes the result file through a temporary file, so a reader never sees half of it.
    def write(self, path):
        folder = os.path.dirname(path)
        if folder:
            os.makedirs(folder, exist_ok=True)
        temporary = path + ".tmp"
        with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(self.to_json(), handle, indent=2, ensure_ascii=False)
            handle.write("\n")
        os.replace(temporary, path)
