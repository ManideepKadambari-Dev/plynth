from . import Step
from ..registry import EXPORT_STEPS

ORDER = 55


# Makes the checked objects ready for the files the way the job's profile asks: the unreal profile joins each
# group into one mesh, the plain one leaves every object as it is.
def run(ctx):
    ctx.state["profile"].finish_objects(ctx)


EXPORT_STEPS.register("finish_objects", Step(ORDER, "Making the objects ready for the files", run))
