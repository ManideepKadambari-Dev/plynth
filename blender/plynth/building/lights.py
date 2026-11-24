import bpy

from . import Step, count, report
from ..registry import BUILD_STEPS

ORDER = 105


# Makes a point light at every light marker, named Light_<floor>_<id>, with the power and colour the job gives.
# Lights have no fixture mesh: a lamp is a catalog asset.
def run(ctx):
    for floor in ctx.params["floors"]:
        for light in floor.get("lights", []):
            try:
                data = bpy.data.lights.new("Light_%s_%s" % (floor["id"], light["id"]), "POINT")
                data.energy = float(light["power"])
                data.color = tuple(light["color"])
                obj = bpy.data.objects.new(data.name, data)
                obj.location = tuple(light["location"])
                obj["plynth_id"] = light["id"]
                obj["plynth_kind"] = "light"
                obj["plynth_light"] = light["kind"]
                ctx.state["collections"][floor["id"]].objects.link(obj)
            except Exception as error:
                report(ctx, "light_failed", "warning", "Light %s could not be made: %s" % (light.get("id"), error),
                       floor["id"], light.get("id"))
                continue
            count(ctx, "lights")


BUILD_STEPS.register("lights", Step(ORDER, "Adding lights", run))
