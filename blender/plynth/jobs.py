import json
import os
import re
import sys

JOB_FORMAT = "plynth-blender-job"
RESULT_FORMAT = "plynth-blender-result"
VERSION = 1
PROGRESS_PREFIX = "PLYNTH:"
REFUSAL_PREFIX = "PLYNTH-REFUSAL:"
JOB_ID_PATTERN = re.compile(r"^[a-z0-9_-]+$")
TASK_PATTERN = re.compile(r"^[a-z][a-z0-9_]*$")


class WorkerError(Exception):
    # Keeps the error code the result file reports, with a plain sentence.
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code
        self.message = message


class Job:
    # Holds the checked fields of a job file.
    def __init__(self, task, job_id, result_file, params):
        self.task = task
        self.job_id = job_id
        self.result_file = result_file
        self.params = params


class Context:
    # What a task works with: its params, a place for warnings and output, and the work folder.
    def __init__(self, job):
        self.job = job
        self.params = job.params
        self.warnings = []
        self.output = {}
        self.state = {}
        self.work_folder = os.path.dirname(os.path.abspath(job.result_file))

    # Adds a warning that does not stop the job.
    def warn(self, code, message):
        self.warnings.append({"code": code, "message": message})


# Reads a job file and checks its format, version and fields. Raises WorkerError bad_job.
def read_job(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError) as error:
        raise WorkerError("bad_job", "The job file cannot be read: %s" % error)
    if not isinstance(data, dict):
        raise WorkerError("bad_job", "The job file is not a JSON object.")
    if data.get("format") != JOB_FORMAT:
        raise WorkerError("bad_job", "The job file has the wrong format.")
    if data.get("version") != VERSION or isinstance(data.get("version"), bool):
        raise WorkerError("bad_job", "The job file has an unsupported version.")
    task = data.get("task")
    if not isinstance(task, str) or not TASK_PATTERN.match(task):
        raise WorkerError("bad_job", "The job has no valid task name.")
    job_id = data.get("job_id")
    if not isinstance(job_id, str) or not JOB_ID_PATTERN.match(job_id):
        raise WorkerError("bad_job", "The job has no valid job id.")
    result_file = data.get("result_file")
    if not isinstance(result_file, str) or not result_file:
        raise WorkerError("bad_job", "The job has no result file.")
    params = data.get("params")
    if not isinstance(params, dict):
        raise WorkerError("bad_job", "The job params are not an object.")
    return Job(task, job_id, result_file, params)


# Reads whatever it can of a broken job file: task, job id and result file, each None when unusable.
def salvage_job(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return None, None, None
    if not isinstance(data, dict):
        return None, None, None

    def text(key):
        value = data.get(key)
        return value if isinstance(value, str) and value else None

    return text("task"), text("job_id"), text("result_file")


# Writes the result through a temporary file and a rename, so a half written result is never read.
def write_result(result_file, result):
    folder = os.path.dirname(os.path.abspath(result_file))
    os.makedirs(folder, exist_ok=True)
    temporary = result_file + ".tmp"
    with open(temporary, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)
    os.replace(temporary, result_file)


# Prints one progress line for the program that started Blender.
def progress(step, index, count, message):
    line = {"progress": {"step": step, "index": index, "count": count, "message": message}}
    sys.stdout.write(PROGRESS_PREFIX + json.dumps(line) + "\n")
    sys.stdout.flush()


# Prints the fixed word that names why a store fetch refused, for the program that started Blender. The word
# is one of a fixed list and never holds a value from the request.
def refusal(word):
    sys.stdout.write(REFUSAL_PREFIX + word + "\n")
    sys.stdout.flush()
