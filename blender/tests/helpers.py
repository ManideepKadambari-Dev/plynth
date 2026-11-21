import json
import os
import shutil
import sys
import tempfile
import unittest

BLENDER_FOLDER = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
REPO_FOLDER = os.path.abspath(os.path.join(BLENDER_FOLDER, ".."))
if BLENDER_FOLDER not in sys.path:
    sys.path.insert(0, BLENDER_FOLDER)

from plynth.runner import run_job_file  # noqa: E402
from schema_check import load_schema, validate  # noqa: E402


class TempFolderTest(unittest.TestCase):
    # Makes a fresh temporary folder for the test.
    def setUp(self):
        self.folder = tempfile.mkdtemp(prefix="plynth_test_")
        self.addCleanup(shutil.rmtree, self.folder, True)

    # Returns a path inside the temporary folder.
    def path(self, *parts):
        return os.path.join(self.folder, *parts).replace("\\", "/")

    # Writes a job file for the task and returns its path and the result file's path.
    def write_job(self, task, task_params, job_id="test-1", **overrides):
        job = {
            "format": "plynth-blender-job",
            "version": 1,
            "task": task,
            "job_id": job_id,
            "result_file": self.path("work", "result.json"),
            "params": task_params,
        }
        job.update(overrides)
        os.makedirs(self.path("work"), exist_ok=True)
        job_file = self.path("work", "job.json")
        with open(job_file, "w", encoding="utf-8") as handle:
            json.dump(job, handle)
        return job_file, job["result_file"]

    # Runs a job through the worker and returns the result read back from its file.
    def run_task(self, task, task_params, **overrides):
        job_file, result_file = self.write_job(task, task_params, **overrides)
        run_job_file(job_file)
        return self.read_result(result_file)

    # Reads a result file.
    def read_result(self, result_file):
        with open(result_file, "r", encoding="utf-8") as handle:
            return json.load(handle)

    # Checks a result and its job against the schemas.
    def assert_result_valid(self, result):
        problems = validate(result, load_schema("blender-result"))
        self.assertEqual(problems, [], "result does not match the schema")
