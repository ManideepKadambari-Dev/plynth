import contextlib
import io
import json
import os
import unittest

import helpers
from plynth import jobs, registry, tasks


# A task that fails, registered once for the error test.
def exploding_task(ctx):
    raise RuntimeError("boom from the task")


# A task that reports a warning and an output.
def warning_task(ctx):
    ctx.warn("high_polycount", "Many triangles.")
    ctx.output = {"value": 7}


registry.discover(tasks)
if registry.TASKS.get("test_boom") is None:
    registry.TASKS.register("test_boom", exploding_task)
    registry.TASKS.register("test_warn", warning_task)


class JobTests(helpers.TempFolderTest):
    # A job with the wrong format gives a bad_job result that matches the schema.
    def test_wrong_format(self):
        result = self.run_task("probe", {}, format="something-else")
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "bad_job")
        self.assertIsNone(result["output"])
        self.assert_result_valid(result)

    # A wrong version, a bad job id and a missing result params are each bad_job.
    def test_other_bad_jobs(self):
        for override in ({"version": 2}, {"job_id": "Bad Id"}, {"params": []}):
            result = self.run_task("probe", {}, **override)
            self.assertEqual(result["error"]["code"], "bad_job", override)

    # A job that is not JSON but names nothing gives no result and no crash.
    def test_unreadable_job(self):
        path = self.path("job.json")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("not json")
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertIsNone(helpers.run_job_file(path))

    # A task that is not in the registry gives unknown_task.
    def test_unknown_task(self):
        result = self.run_task("no_such_task", {})
        self.assertEqual(result["error"]["code"], "unknown_task")
        self.assert_result_valid(result)

    # An exception inside a task gives an internal error holding the exception text.
    def test_exception_is_internal(self):
        with contextlib.redirect_stderr(io.StringIO()):
            result = self.run_task("test_boom", {})
        self.assertEqual(result["error"]["code"], "internal")
        self.assertIn("boom from the task", result["error"]["message"])
        self.assertIn("RuntimeError", result["error"]["message"])
        self.assert_result_valid(result)

    # Warnings and output of a task reach the result.
    def test_warnings_and_output(self):
        result = self.run_task("test_warn", {})
        self.assertTrue(result["ok"])
        self.assertEqual(result["output"], {"value": 7})
        self.assertEqual(result["warnings"], [{"code": "high_polycount", "message": "Many triangles."}])
        self.assertEqual(result["task"], "test_warn")
        self.assertEqual(result["job_id"], "test-1")

    # The result is written through a temporary file that is gone afterwards.
    def test_result_written_through_rename(self):
        job_file, result_file = self.write_job("probe", {})
        helpers.run_job_file(job_file)
        names = os.listdir(os.path.dirname(result_file))
        self.assertIn("result.json", names)
        self.assertEqual([name for name in names if name.endswith(".tmp")], [])

    # Writing a result replaces an older one in one step.
    def test_write_result_replaces(self):
        target = self.path("out", "r.json")
        jobs.write_result(target, {"a": 1})
        jobs.write_result(target, {"a": 2})
        with open(target, "r", encoding="utf-8") as handle:
            self.assertEqual(json.load(handle), {"a": 2})
        self.assertEqual(os.listdir(self.path("out")), ["r.json"])

    # A progress line starts with the prefix and the rest is JSON.
    def test_progress_line(self):
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            jobs.progress("apply_transforms", 3, 11, "Applying transforms")
        line = buffer.getvalue().strip()
        self.assertTrue(line.startswith("PLYNTH:"))
        data = json.loads(line[len("PLYNTH:"):])
        self.assertEqual(data, {"progress": {"step": "apply_transforms", "index": 3, "count": 11,
                                             "message": "Applying transforms"}})

    # The work folder is the folder that holds the result file.
    def test_context_work_folder(self):
        job_file, result_file = self.write_job("probe", {})
        job = jobs.read_job(job_file)
        ctx = jobs.Context(job)
        self.assertEqual(ctx.work_folder.replace("\\", "/"), self.path("work"))


if __name__ == "__main__":
    unittest.main()
