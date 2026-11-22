import os

import bpy
import numpy as np

from plynth.materials import write_png


# Adds a node of a type to a tree.
def node(tree, kind, x=0, y=0, **props):
    created = tree.nodes.new(kind)
    created.location = (x, y)
    for key, value in props.items():
        setattr(created, key, value)
    return created


# Makes a material with a Principled BSDF on a material output and returns (material, principled).
def new_material(name):
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    tree = material.node_tree
    principled = next(n for n in tree.nodes if n.bl_idname == "ShaderNodeBsdfPrincipled")
    return material, principled


# Writes the materials into a .blend file that holds nothing else, then removes them from the session.
def write_blend(path, materials):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    for material in materials:
        material.use_fake_user = True
    bpy.data.libraries.write(path, set(materials), fake_user=True)
    for material in materials:
        tree = material.node_tree
        images = [n.image for n in tree.nodes if n.bl_idname == "ShaderNodeTexImage" and n.image]
        bpy.data.materials.remove(material)
        for image in images:
            if image.name in bpy.data.images:
                bpy.data.images.remove(image)


# The gate material: every channel goes through a node that is not an image texture.
def gate_material(name="gate"):
    material, principled = new_material(name)
    tree = material.node_tree
    color = node(tree, "ShaderNodeRGB")
    color.outputs[0].default_value = (0.8, 0.2, 0.1, 1.0)
    mix = node(tree, "ShaderNodeMix", data_type="RGBA")
    mix.inputs["Factor"].default_value = 0.0
    tree.links.new(color.outputs[0], mix.inputs["A"])
    tree.links.new(color.outputs[0], mix.inputs["B"])
    tree.links.new(mix.outputs["Result"], principled.inputs["Base Color"])
    rough = node(tree, "ShaderNodeValue")
    rough.outputs[0].default_value = 0.5
    rough_math = node(tree, "ShaderNodeMath", operation="ADD")
    rough_math.inputs[1].default_value = 0.0
    tree.links.new(rough.outputs[0], rough_math.inputs[0])
    tree.links.new(rough_math.outputs[0], principled.inputs["Roughness"])
    metal = node(tree, "ShaderNodeValue")
    metal.outputs[0].default_value = 1.0
    metal_math = node(tree, "ShaderNodeMath", operation="MULTIPLY")
    metal_math.inputs[1].default_value = 1.0
    tree.links.new(metal.outputs[0], metal_math.inputs[0])
    tree.links.new(metal_math.outputs[0], principled.inputs["Metallic"])
    bump = node(tree, "ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.0
    tree.links.new(bump.outputs["Normal"], principled.inputs["Normal"])
    return material


# Adds an image texture node reading a file with the given colour space.
def image_node(tree, path, colorspace, **props):
    image = bpy.data.images.load(path, check_existing=False)
    image.colorspace_settings.name = colorspace
    return node(tree, "ShaderNodeTexImage", image=image, **props)


# A material whose base color, roughness, metallic and normal are image textures linked straight in.
# files maps base_color, roughness, metallic and normal to image paths.
def image_material(name, files):
    material, principled = new_material(name)
    tree = material.node_tree
    base = image_node(tree, files["base_color"], "sRGB")
    tree.links.new(base.outputs["Color"], principled.inputs["Base Color"])
    for channel, socket in (("roughness", "Roughness"), ("metallic", "Metallic")):
        texture = image_node(tree, files[channel], "Non-Color")
        tree.links.new(texture.outputs["Color"], principled.inputs[socket])
    texture = image_node(tree, files["normal"], "Non-Color")
    normal = node(tree, "ShaderNodeNormalMap")
    tree.links.new(texture.outputs["Color"], normal.inputs["Color"])
    tree.links.new(normal.outputs["Normal"], principled.inputs["Normal"])
    return material


# A material with a plain base color and a Noise texture into the base color.
def noise_material(name, scale=5.0):
    material, principled = new_material(name)
    tree = material.node_tree
    noise = node(tree, "ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = scale
    noise.inputs["Detail"].default_value = 0.0
    tree.links.new(noise.outputs["Color"], principled.inputs["Base Color"])
    return material


# A material with a plain value look and nothing linked.
def plain_material(name, color=(0.5, 0.25, 0.1, 1.0), roughness=0.3, metallic=0.7):
    material, principled = new_material(name)
    principled.inputs["Base Color"].default_value = color
    principled.inputs["Roughness"].default_value = roughness
    principled.inputs["Metallic"].default_value = metallic
    return material


# Writes a small png of random pixels and returns the pixels (top row first).
def write_noise_png(path, size=16, seed=3):
    data = np.random.RandomState(seed).randint(0, 256, (size, size, 3)).astype(np.uint8)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    write_png(path, data)
    return data
