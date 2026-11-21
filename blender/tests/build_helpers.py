import json
import os

import helpers

DATA_FOLDER = os.path.join(helpers.BLENDER_FOLDER, "tests", "data", "buildjob")


# Reads a job fixture from tests/data/buildjob and returns the job params. The output folder is filled in by the test.
def load_params(name):
    with open(os.path.join(DATA_FOLDER, name + ".json"), "r", encoding="utf-8") as handle:
        return json.load(handle)["params"]


class BuildTest(helpers.TempFolderTest):
    # Runs the build task on params from a fixture and returns the result. Extra keyword arguments replace params.
    def build(self, params, **overrides):
        params = dict(params)
        params["output_folder"] = self.path("build")
        params.update(overrides)
        return self.run_task("build", params, job_id="build-1")

    # Returns the progress lines the last run printed, as dictionaries.
    def progress_lines(self):
        lines = [line for line in self.printed.splitlines() if line.startswith("PLYNTH:")]
        return [json.loads(line[len("PLYNTH:"):])["progress"] for line in lines]

    # Checks a job file's params against the job schema.
    def assert_job_valid(self, name):
        from schema_check import load_schema, validate
        with open(os.path.join(DATA_FOLDER, name + ".json"), "r", encoding="utf-8") as handle:
            job = json.load(handle)
        self.assertEqual(validate(job, load_schema("blender-job")), [], "job does not match the schema")
