import bpy
from mathutils import Matrix, Vector

from . import Step, count, report
from ..registry import BUILD_STEPS

ORDER = 105
MIN_LENGTH = 1e-6


# Makes a point light at every light marker, named Light_<floor>_<id>, with the power and colour the job gives,
# then an area light for every straight piece of a light strip, named Light_<strip>_<NN>. Lights have no fixture
# mesh: a lamp is a catalog asset and a strip's housing is a build part.
def run(ctx):
    numbers = {}
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
        for light in floor.get("strip_lights", []):
            number = numbers.get(light.get("id"), 0)
            numbers[light.get("id")] = number + 1
            try:
                obj = strip_light(light, "Light_%s_%02d" % (light["id"], number))
                ctx.state["collections"][floor["id"]].objects.link(obj)
            except Exception as error:
                report(ctx, "light_failed", "warning", "A piece of the light strip %s could not be made: %s" % (
                    light.get("id"), error), floor["id"], light.get("id"))
                continue
            count(ctx, "lights")


# Makes the area light of one strip piece: a rectangle as long as the piece and as wide as the strip, in the
# middle of the piece, shining the way it faces. An area light shines along its own minus Z.
def strip_light(light, name):
    start = Vector(tuple(float(c) for c in light["start"]))
    end = Vector(tuple(float(c) for c in light["end"]))
    facing = Vector(tuple(float(c) for c in light["facing"]))
    along = end - start
    if along.length < MIN_LENGTH:
        raise ValueError("it has no length")
    if facing.length < MIN_LENGTH:
        raise ValueError("it faces no way")
    facing.normalize()
    x_axis = along - facing * along.dot(facing)
    if x_axis.length < MIN_LENGTH:
        raise ValueError("it faces along its own length")
    x_axis.normalize()
    z_axis = -facing
    y_axis = z_axis.cross(x_axis)
    data = bpy.data.lights.new(name, "AREA")
    data.shape = "RECTANGLE"
    data.size = along.length
    data.size_y = float(light["width"])
    data.energy = float(light["power"])
    data.color = tuple(float(c) for c in light["colour"])
    obj = bpy.data.objects.new(name, data)
    matrix = Matrix((x_axis, y_axis, z_axis)).transposed().to_4x4()
    matrix.translation = (start + end) / 2.0
    obj.matrix_world = matrix
    obj["plynth_id"] = light["id"]
    obj["plynth_kind"] = "light"
    obj["plynth_light"] = "strip"
    return obj


BUILD_STEPS.register("lights", Step(ORDER, "Adding lights", run))
