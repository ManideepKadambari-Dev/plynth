"""Makes buildjob/from_core_materials.json from buildjob/from_core.json (the job core makes).

Inside faces get white_paint, outside faces red_brick, wall tops red_brick, floors grey_tile and ceilings
white_paint. The library folder stays a placeholder (@LIBRARY@) that the test fills in.
Run with: python make_from_core_materials.py
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

FOLDER = os.path.join(HERE, "buildjob")
CHANNELS = ("base_color", "normal", "roughness", "metallic", "opacity", "occlusion")
MATERIALS = {
    "grey_tile": ({"width": 0.6, "height": 0.6}, {"base_color": "png", "normal": "png", "roughness": "png",
                                                  "metallic": "png", "opacity": None, "occlusion": "png"}),
    "red_brick": ({"width": 1.2, "height": 0.6}, {"base_color": "jpg", "normal": "png", "roughness": "png",
                                                  "metallic": "png", "opacity": None, "occlusion": None}),
    "white_paint": ({"width": 1.0, "height": 1.0}, {"base_color": "png", "normal": "png", "roughness": "png",
                                                    "metallic": "png", "opacity": None, "occlusion": None}),
}
SLOT_MATERIAL = {"Wall_Inside": "white_paint", "Wall_Outside": "red_brick", "Wall_Top": "red_brick",
                 "Floor": "grey_tile", "Ceiling": "white_paint", "Threshold": "grey_tile"}


# Returns the job entry of one library material with placeholder paths.
def entry(material_id):
    tile, extensions = MATERIALS[material_id]
    maps = {}
    for channel in CHANNELS:
        extension = extensions[channel]
        maps[channel] = (None if extension is None else
                         "@LIBRARY@/materials/items/%s/textures/%s.%s" % (material_id, channel, extension))
    return {"id": material_id, "tile_size": tile, "maps": maps}


def main():
    with open(os.path.join(FOLDER, "from_core.json"), "r", encoding="utf-8") as handle:
        job = json.load(handle)
    params = job["params"]
    params["materials"] = [entry(name) for name in sorted(MATERIALS)]
    for floor in params["floors"]:
        for solid in floor["wall_solids"]:
            solid["top_material"] = "red_brick"
            for ring in [solid["outline"]] + solid["holes"]:
                for edge in ring["edges"]:
                    edge["material"] = None if edge["side"] is None else (
                        "white_paint" if edge["room"] is not None else "red_brick")
        for room in floor["rooms"]:
            room["floor_material"] = "grey_tile"
            room["ceiling_material"] = "white_paint"
        for opening in floor["openings"]:
            opening["materials"]["reveal"] = "white_paint"
            opening["materials"]["threshold"] = "grey_tile"
    for slot in params["slots"]:
        slot["material"] = SLOT_MATERIAL.get(slot["surface"])
    with open(os.path.join(FOLDER, "from_core_materials.json"), "w", encoding="utf-8", newline="\n") as handle:
        handle.write(json.dumps(job, indent=2) + "\n")


if __name__ == "__main__":
    main()
