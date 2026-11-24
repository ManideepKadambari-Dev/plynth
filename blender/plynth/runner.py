import importlib
import pkgutil
import time
import traceback

from . import registry
from . import tasks
from .jobs import RESULT_FORMAT, VERSION, Context, WorkerError, read_job, salvage_job, write_result


# Returns the running Blender's version as text such as 5.2.0.
def blender_version():
    try:
        import bpy
        return ".".join(str(part) for part in bpy.app.version)
    except Exception:
        return "0.0.0"


# Finds the function of a task. Imports only the module named after the task when it registers it, so a fetch
# loads no other task's code; otherwise every task module.
def load_task(task):
    names = {name for _, name, _ in pkgutil.iter_modules(tasks.__path__) if not name.startswith("_")}
    if task in names:
        importlib.import_module(tasks.__name__ + "." + task)
        if registry.TASKS.get(task) is not None:
            return registry.TASKS.get(task)
    registry.discover(tasks)
    return registry.TASKS.get(task)


# Builds the result dictionary of the result schema.
def make_result(task, job_id, started, ctx=None, error=None):
    ok = error is None
    return {
        "format": RESULT_FORMAT,
        "version": VERSION,
        "task": task,
        "job_id": job_id,
        "ok": ok,
        "error": None if ok else {"code": error.code, "message": error.message or "The task failed."},
        "blender_version": blender_version(),
        "warnings": list(ctx.warnings) if ctx is not None else [],
        "elapsed_ms": int((time.monotonic() - started) * 1000),
        "output": ctx.output if ok and ctx is not None else None,
    }


# Runs the job in the file and writes its result file. Returns the result, or None when the job gave no
# result file to write to. An error inside a task never escapes without a result.
def run_job_file(path):
    started = time.monotonic()
    ctx = None
    try:
        job = read_job(path)
        task, job_id, result_file = job.task, job.job_id, job.result_file
    except WorkerError as error:
        task, job_id, result_file = salvage_job(path)
        if result_file is None:
            print("PLYNTH worker: %s" % error.message)
            return None
        result = make_result(task or "unknown", job_id or "unknown", started, error=error)
        write_result(result_file, result)
        return result

    try:
        run = load_task(task)
        if run is None:
            raise WorkerError("unknown_task", "The task '%s' is not known." % task)
        ctx = Context(job)
        run(ctx)
        error = None
    except WorkerError as caught:
        error = caught
    except Exception as caught:
        traceback.print_exc()
        error = WorkerError("internal", "%s: %s" % (type(caught).__name__, caught))
    result = make_result(task, job_id, started, ctx, error)
    write_result(result_file, result)
    return result
