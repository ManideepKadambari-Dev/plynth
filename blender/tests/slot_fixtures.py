import re

SURFACE_OF_NAME = {}
PREVIEW_FOLDER = "preview_textures"
PREVIEW_SIZE = 1024


# Turns a snake case word into PascalCase. A word that starts with a digit gets "Room" in front.
def pascal(text):
    parts = [p for p in re.split(r"[^A-Za-z0-9]+", text or "") if p]
    word = "".join(p[:1].upper() + p[1:] for p in parts)
    if not word:
        return "Room"
    return word if word[0].isalpha() else "Room" + word


# Returns the slot name of a use: the base name, with the material added when two uses of the name differ.
def finish_names(uses):
    materials = {}
    for base, material, _ in uses:
        materials.setdefault(base, set()).add(material)
    names = []
    for base, material, _ in uses:
        if len(materials[base]) > 1:
            names.append("%s_%s" % (base, pascal(material) if material else "Neutral"))
        else:
            names.append(base)
    return names


# Fills in every slot field and the slots list of a build job from its material fields, and the other fields
# milestone 10 added (preview texture parameters, asset materials, collision). Safe to run again after the
# material fields change.
def upgrade(params):
    params.setdefault("preview_textures_folder", PREVIEW_FOLDER)
    params.setdefault("preview_texture_size", PREVIEW_SIZE)
    for asset in params.get("assets", []):
        asset.setdefault("materials", [])
    uses = []

    def use(base, material, surface, holder, key):
        uses.append((base, material, surface))
        holder.append((len(uses) - 1, key))

    targets = []
    for floor in params["floors"]:
        floor.setdefault("collision", [])
        labels = {r["id"]: pascal(r.get("type")) if r.get("type") else "Room" for r in floor["rooms"]}
        for room in floor["rooms"]:
            label = labels[room["id"]]
            target = []
            use("%s_Floor" % label, room["floor_material"], "Floor", target, (room, "floor_slot"))
            use("%s_Ceiling" % label, room["ceiling_material"], "Ceiling", target, (room, "ceiling_slot"))
            targets.append(target)
        wall_faces = {}
        for solid in floor["wall_solids"]:
            target = []
            use("Wall_Top", solid["top_material"], "Wall_Top", target, (solid, "top_slot"))
            targets.append(target)
            for ring in [solid["outline"]] + solid["holes"]:
                for edge in ring["edges"]:
                    target = []
                    if edge["side"] is None:
                        edge["material"] = solid["top_material"]
                        use("Wall_Top", edge["material"], "Wall_Top", target, (edge, "slot"))
                    elif edge["room"] is not None:
                        use("%s_Wall" % labels.get(edge["room"], "Room"), edge["material"], "Wall_Inside", target,
                            (edge, "slot"))
                    else:
                        use("Exterior_Wall", edge["material"], "Wall_Outside", target, (edge, "slot"))
                    targets.append(target)
                    if edge["side"] is not None and (edge["room"] is not None or edge["wall"] not in wall_faces):
                        wall_faces[edge["wall"]] = edge
        for opening in floor["openings"]:
            target = []
            edge = wall_faces.get(opening["wall"])
            if edge is not None:
                if edge["room"] is not None:
                    base = "%s_Wall" % labels.get(edge["room"], "Room")
                else:
                    base = "Exterior_Wall"
                material = opening["materials"]["reveal"]
                use(base, material, "Reveal", target, (opening.setdefault("slots", {}), "reveal"))
            else:
                use("Exterior_Wall", opening["materials"]["reveal"], "Reveal", target,
                    (opening.setdefault("slots", {}), "reveal"))
            if opening["threshold"] and edge is not None and edge["room"] is not None:
                use("%s_Floor" % labels.get(edge["room"], "Room"), opening["materials"]["threshold"], "Floor", target,
                    (opening["slots"], "threshold"))
            else:
                use("Threshold", opening["materials"]["threshold"], "Threshold", target,
                    (opening["slots"], "threshold"))
            for key, name in (("door", "Door"), ("frame", "Frame"), ("glass", "Glass"), ("handle", "Handle")):
                use(name, opening["materials"][key], name, target, (opening["slots"], key))
            targets.append(target)
    names = finish_names(uses)
    table = {}
    for target in targets:
        for index, (holder, key) in target:
            holder[key] = names[index]
            base, material, surface = uses[index]
            table.setdefault(names[index], {"name": names[index], "material": material, "surface": surface})
    params["slots"] = [table[name] for name in sorted(table)]
    return params

