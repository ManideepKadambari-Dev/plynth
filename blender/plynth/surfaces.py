import bpy

# Surface name: (sRGB base color, roughness, metallic, alpha). Every face of a built object carries one of
# these through its material slot.
FLOOR_LOOK = ((0.55, 0.50, 0.44), 0.7, 0.0, 1.0)
INSIDE_LOOK = ((0.82, 0.80, 0.77), 0.9, 0.0, 1.0)
LOOKS = {
    "Wall_Inside": INSIDE_LOOK,
    "Wall_Outside": ((0.70, 0.68, 0.65), 0.9, 0.0, 1.0),
    "Wall_Top": ((0.60, 0.59, 0.57), 0.9, 0.0, 1.0),
    "Reveal": INSIDE_LOOK,
    "Threshold": FLOOR_LOOK,
    "Floor": FLOOR_LOOK,
    "Ceiling": ((0.93, 0.93, 0.92), 0.95, 0.0, 1.0),
    "Door": ((0.62, 0.52, 0.42), 0.6, 0.0, 1.0),
    "Frame": ((0.88, 0.88, 0.87), 0.5, 0.0, 1.0),
    "Glass": ((0.80, 0.88, 0.92), 0.05, 0.0, 0.25),
    "Handle": ((0.70, 0.70, 0.72), 0.3, 1.0, 1.0),
    "Stair": ((0.60, 0.46, 0.32), 0.65, 0.0, 1.0),
    "Roof": ((0.52, 0.28, 0.22), 0.85, 0.0, 1.0),
    "Flat_Roof": ((0.45, 0.45, 0.44), 0.9, 0.0, 1.0),
    "Soffit": ((0.90, 0.90, 0.89), 0.95, 0.0, 1.0),
    "Fascia": ((0.40, 0.38, 0.36), 0.8, 0.0, 1.0),
}


# Returns the material called name, making an empty one when it does not exist yet.
def material(name):
    found = bpy.data.materials.get(name)
    if found is None:
        found = bpy.data.materials.new(name)
    return found


# Returns the index of the surface slot on the object, adding the slot when it is missing.
def slot_index(obj, name):
    wanted = material(name)
    for index, existing in enumerate(obj.data.materials):
        if existing == wanted:
            return index
    obj.data.materials.append(wanted)
    return len(obj.data.materials) - 1


# Converts one sRGB value from 0 to 1 to the linear value Blender colors use.
def to_linear(value):
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


# Gives a material the neutral Principled look of its surface name.
def apply_look(mat, look):
    color, roughness, metallic, alpha = look
    try:
        mat.use_nodes = True
    except Exception:
        pass
    tree = mat.node_tree
    node = tree.nodes.get("Principled BSDF")
    if node is None:
        node = tree.nodes.new("ShaderNodeBsdfPrincipled")
        output = tree.nodes.get("Material Output") or tree.nodes.new("ShaderNodeOutputMaterial")
        tree.links.new(node.outputs["BSDF"], output.inputs["Surface"])
    linear = (to_linear(color[0]), to_linear(color[1]), to_linear(color[2]))
    node.inputs["Base Color"].default_value = linear + (1.0,)
    node.inputs["Roughness"].default_value = roughness
    node.inputs["Metallic"].default_value = metallic
    node.inputs["Alpha"].default_value = alpha
    mat.diffuse_color = linear + (alpha,)
