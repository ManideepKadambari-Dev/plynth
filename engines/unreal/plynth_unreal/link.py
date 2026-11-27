import time
import traceback

from . import jobfile
from .errors import LinkError
from .result import Result


# Runs the job in the file, writes the result file it names and prints one line, "plynth_link: ok" or
# "plynth_link: error <code>". Returns 0 when the job worked.
def run(job_path):
    started = time.monotonic()
    result_file, job_id = jobfile.answer_address(job_path)
    result = Result(job_id)
    try:
        job = jobfile.read(job_path)
    except LinkError as error:
        return finish(result, result_file, started, error)
    result_file = job["result_file"]
    result.job_id = job["job_id"]
    result.content_path = job["content_path"]
    try:
        import unreal
        if not hasattr(unreal, "get_editor_subsystem"):
            raise ImportError("no editor subsystems")
    except ImportError:
        return finish(result, result_file, started,
                      LinkError("not_editor", "The Plynth script must run inside the Unreal Editor."))
    return deliver(job, result, result_file, started)


# Runs every delivery step in order, then the self checks and the picture when the job asks for them.
def deliver(job, result, result_file, started):
    from . import checks, steps, ue
    from .context import Context
    from .registry import CHECKS, STEPS, discover

    result.engine_version = ue.engine_version()
    result.project_name = ue.project_name()
    discover(steps)
    discover(checks)
    ctx = Context(job, result)
    for entry in STEPS.ordered():
        if not entry.applies(ctx):
            continue
        try:
            entry.run(ctx)
        except LinkError as error:
            return finish(result, result_file, started, error)
        except Exception as error:
            ue.warn("step %s failed:\n%s" % (entry.name, traceback.format_exc()))
            return finish(result, result_file, started, LinkError(entry.code, "%s failed: %s" % (entry.name, error)))
    if job["checks"] is not None:
        run_checks(ctx, CHECKS.ordered())
        if job["checks"]["screenshot"]:
            from . import screenshot
            result.screenshot = screenshot.take(ctx, job["checks"]["screenshot"])
    return finish(result, result_file, started, None)


# Runs each self check; a check that breaks counts as failed with what went wrong.
def run_checks(ctx, entries):
    for entry in entries:
        try:
            status, message = entry.run(ctx)
        except Exception as error:
            status, message = "fail", "The check could not run: %s" % error
        ctx.result.check(entry.id, status, message)


# Writes the result file and prints the closing line.
def finish(result, result_file, started, error):
    if error is not None:
        result.fail(error.code, error.message)
    result.ms = int((time.monotonic() - started) * 1000)
    if result_file:
        try:
            result.write(result_file)
        except OSError as write_error:
            print("The result file %s could not be written: %s" % (result_file, write_error))
    if error is None:
        print("plynth_link: ok")
        return 0
    print("plynth_link: error %s" % error.code)
    print(error.message)
    return 1
