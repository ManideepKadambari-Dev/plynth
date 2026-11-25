"""Makes the sample projects samples/projects/apartment and samples/projects/house with plynth commands.

It works in a scratch folder: a library with the sample items, then each project from the sample plans (scale,
detection, room types, furniture, materials, a catalog door, lights, set dressing, the license report, and for
the house the upper floor, a stair and roofs). Only the project files are copied into samples/projects, and samples.json lists both.
Run it in a shell whose PLYNTH_CONFIG_DIR and Blender variables keep it away from your own settings:

    python samples/tools/make_sample_projects.py --plynth <plynth.com> --scratch <empty folder>

--library <folder> uses a library that already holds the sample items instead of making one.
"""
import argparse
import json
import math
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLES = os.path.dirname(HERE)
PLANS = os.path.join(SAMPLES, "plans")
PIXELS_PER_METRE = 100
PROJECT_FILES = ("project.json", "plan.json", "export-options.json")
GAP = 0.03
DOOR_CLEARANCE = 0.5
PASSAGE_CLEARANCE = 0.4
STAIR_CLEARANCE = 0.35

PROJECTS = [
    {"id": "apartment", "name": "Apartment",
     "description": "A furnished one floor apartment with two bedrooms, a bathroom, a kitchen and a living room.",
     "image": "apartment-01.png"},
    {"id": "house", "name": "House",
     "description": "A two floor house with a stair, a gable roof and a flat roof over the one storey strip.",
     "image": "house-01-ground.png"},
]


class Plynth:
    # Remembers the program, the library and the project folder the commands work on.
    def __init__(self, program, library):
        self.program = program
        self.library = library
        self.project = None

    # Runs one command and returns its result. A refusal raises unless allowed, and then returns None.
    def run(self, *args, allow_refusal=False, library=False, project=True):
        command = [self.program] + args[0].split() + [str(a) for a in args[1:]]
        if project and self.project:
            command += ["--project", self.project]
        if library:
            command += ["--library", self.library]
        done = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
        try:
            answer = json.loads(done.stdout)
        except ValueError:
            raise SystemExit("No answer from %s:\n%s\n%s" % (" ".join(command), done.stdout, done.stderr))
        if not answer.get("ok"):
            if allow_refusal:
                return None
            raise SystemExit("%s was refused: %s" % (" ".join(args[:2]), answer.get("error")))
        for warning in answer.get("warnings", []):
            print("  warning: %s" % warning.get("message"))
        return answer["result"]


# Returns the inside box of a room (left, top, right, bottom in plan metres) and its walls by side, from the
# straight walls around it. Plan y grows down the plan.
def room_box(plynth, floor, room):
    points = {p["id"]: p for p in plynth.run("point list", "--floor", floor)["points"]}
    walls = {w["id"]: w for w in plynth.run("wall list", "--floor", floor)["walls"]}
    cx, cy = room["label"]["x"], room["label"]["y"]
    sides = {"left": None, "right": None, "top": None, "bottom": None}
    for wall_id in room["walls"]:
        wall = walls[wall_id]
        a, b = points[wall["from"]], points[wall["to"]]
        half = wall["thickness"] / 2.0
        if abs(a["x"] - b["x"]) < 1e-6:
            side, face = ("left", a["x"] + half) if a["x"] < cx else ("right", a["x"] - half)
        elif abs(a["y"] - b["y"]) < 1e-6:
            side, face = ("top", a["y"] + half) if a["y"] < cy else ("bottom", a["y"] - half)
        else:
            continue
        better = sides[side] is None or (face > sides[side][0] if side in ("left", "top") else face < sides[side][0])
        if better:
            sides[side] = (face, [])
        if abs(sides[side][0] - face) < 1e-6:
            sides[side][1].append(wall_id)
    box = (sides["left"][0], sides["top"][0], sides["right"][0], sides["bottom"][0])
    return box, {side: value[1] for side, value in sides.items()}


# Returns the footprint box of an item of a size at a place and turn (0, 90, 180 or 270).
def footprint(x, y, size, rotation):
    across, deep = (size["width"], size["depth"]) if rotation in (0, 180) else (size["depth"], size["width"])
    return (x - across / 2, y - deep / 2, x + across / 2, y + deep / 2)


# Returns true when a box comes nearer than the clearance to the middle of a door or a plain opening, or stands
# in front of a window when it is taller than a sill.
def blocks_opening(box, openings, height, door_clearance=DOOR_CLEARANCE):
    for opening in openings:
        ox, oy = opening["centre"]["x"], opening["centre"]["y"]
        dx = max(box[0] - ox, 0.0, ox - box[2])
        dy = max(box[1] - oy, 0.0, oy - box[3])
        if opening["type"] == "window":
            reach = opening["width"] / 2 + 0.1 if height > 0.9 else -1.0
        elif opening["type"] == "plain":
            reach = opening["width"] / 2 + PASSAGE_CLEARANCE
        else:
            reach = opening["width"] / 2 + door_clearance
        if math.hypot(dx, dy) < reach:
            return True
    return False


class Room:
    # Holds what placing needs about one room: its id, box, walls by side and the openings of its floor.
    def __init__(self, plynth, floor, room, sizes):
        self.plynth = plynth
        self.floor = floor
        self.id = room["id"]
        self.box, self.walls = room_box(plynth, floor, room)
        self.openings = plynth.run("opening list", "--floor", floor)["openings"]
        self.sizes = sizes

    # Places an asset at the first of the places that is clear of doors and has no problem. Returns the item, or
    # None when no place works and the item is not required.
    def place_first(self, asset, places, z=0.0, required=True):
        size = self.sizes[asset]
        for x, y, rotation in places:
            if z == 0.0 and blocks_opening(footprint(x, y, size, rotation), self.openings, size["height"]):
                continue
            result = self.plynth.run("place", asset, "--floor", self.floor, "--room", self.id, "--x", round(x, 3),
                                     "--y", round(y, 3), "--rot", rotation, "--z", z, "--strict",
                                     allow_refusal=True, library=True)
            if result:
                return result["item"]
        if not required:
            return None
        raise SystemExit("No clear place for %s in room %s." % (asset, self.id))

    # Places an asset with its back to a wall, trying the sides in order and spots along each side.
    def against(self, asset, sides, spots=(0.5, 0.3, 0.7, 0.15, 0.85)):
        size = self.sizes[asset]
        left, top, right, bottom = self.box
        places = []
        for side in sides:
            for spot in spots:
                if side == "top":
                    places.append((left + spot * (right - left), top + size["depth"] / 2 + GAP, 0))
                elif side == "bottom":
                    places.append((left + spot * (right - left), bottom - size["depth"] / 2 - GAP, 180))
                elif side == "left":
                    places.append((left + size["depth"] / 2 + GAP, top + spot * (bottom - top), 270))
                else:
                    places.append((right - size["depth"] / 2 - GAP, top + spot * (bottom - top), 90))
        return self.place_first(asset, places)

    # Places a table with a chair at each long side and a vase on it, as near the middle of the room as the doors
    # allow, along or across the room.
    def dining(self):
        left, top, right, bottom = self.box
        cx, cy = (left + right) / 2, (top + bottom) / 2
        reach = self.sizes["table"]["depth"] / 2 + self.sizes["chair"]["depth"] / 2 + 0.05
        half_long = self.sizes["table"]["width"] / 2
        half_deep = reach + self.sizes["chair"]["depth"] / 2
        spots = sorted(((cx + dx * 0.2, cy + dy * 0.2) for dx in range(-12, 13) for dy in range(-12, 13)),
                       key=lambda p: math.hypot(p[0] - cx, p[1] - cy))
        for tx, ty in spots:
            for turned in (False, True):
                if turned:
                    box = (tx - half_deep, ty - half_long, tx + half_deep, ty + half_long)
                    chairs = [(tx - reach, ty - 0.3, 270), (tx + reach, ty + 0.3, 90)]
                else:
                    box = (tx - half_long, ty - half_deep, tx + half_long, ty + half_deep)
                    chairs = [(tx - 0.3, ty - reach, 0), (tx + 0.3, ty + reach, 180)]
                inside = left + GAP < box[0] and top + GAP < box[1] and box[2] < right - GAP and box[3] < bottom - GAP
                if not inside or blocks_opening(box, self.openings, 0.9):
                    continue
                table = self.place_first("table", [(tx, ty, 90 if turned else 0)], required=False)
                if table is None:
                    continue
                placed = [self.place_first("chair", [chair], required=False) for chair in chairs]
                if all(placed):
                    self.place_first("vase", [(tx, ty, 0)], z=self.sizes["table"]["height"])
                    return
                for item in [table] + [c for c in placed if c]:
                    self.plynth.run("item remove", item["id"])
        raise SystemExit("No clear place for a table and chairs in room %s." % self.id)

    # Hangs a TV on a wall of the room and puts the sofa in front of it, facing it. A TV spot with no room for
    # the sofa is given up for the next one.
    def lounge(self, sides):
        facing = {0: (0.0, 1.0), 90: (-1.0, 0.0), 180: (0.0, -1.0), 270: (1.0, 0.0)}
        turns = {(0.0, 1.0): 180, (-1.0, 0.0): 270, (0.0, -1.0): 0, (1.0, 0.0): 90}
        for tv in self.wall_places("tv", sides):
            front = facing[round(tv["rotation"]) % 360]
            places = [(tv["x"] + front[0] * distance + front[1] * shift,
                       tv["y"] + front[1] * distance + front[0] * shift, turns[front])
                      for distance in (2.6, 2.9, 2.3, 3.2) for shift in (0.0, 0.3, -0.3, 0.6, -0.6)]
            if self.place_first("sofa", places, required=False):
                return
            self.plynth.run("item remove", tv["id"])
        raise SystemExit("No place for a TV and a sofa in room %s." % self.id)

    # Hangs an asset on a wall of the room at the first side and spot that works.
    def on_wall(self, asset, sides):
        for item in self.wall_places(asset, sides):
            return item
        raise SystemExit("No clear wall for %s in room %s." % (asset, self.id))

    # Hangs an asset on each wall spot of the sides in turn that takes it, giving back each item placed; the
    # caller removes one it does not keep.
    def wall_places(self, asset, sides, spots=(0.5, 0.4, 0.6, 0.3, 0.7, 0.2, 0.8, 0.15, 0.85, 0.1, 0.9)):
        walls = self.plynth.run("wall list", "--floor", self.floor)["walls"]
        lengths = {w["id"]: w["length"] for w in walls}
        for side in sides:
            for wall in self.walls[side]:
                for spot in spots:
                    result = self.plynth.run("place", asset, "--floor", self.floor, "--wall", wall, "--room", self.id,
                                             "--offset", round(spot * lengths[wall], 3), "--strict",
                                             allow_refusal=True, library=True)
                    if result:
                        yield result["item"]

    # Puts a ceiling light in the middle of the room.
    def light(self):
        left, top, right, bottom = self.box
        self.plynth.run("light add", "--floor", self.floor, "--x", round((left + right) / 2, 3),
                        "--y", round((top + bottom) / 2, 3))


# Runs detection on a floor and gives each detected room the type of the plan room whose middle it holds.
def detect_rooms(plynth, floor, types):
    plynth.run("detect", "--floor", floor, "--apply")
    rooms = plynth.run("room list", "--floor", floor)["rooms"]
    found = {}
    for (x, y), type_id in types:
        room = next((r for r in rooms if r["id"] not in found.values() and near_room(plynth, floor, r, x, y)), None)
        if room is None:
            raise SystemExit("Detection found no room at (%.2f, %.2f) on %s." % (x, y, floor))
        plynth.run("room set", room["id"], "--type", type_id)
        found[type_id if type_id not in found else type_id + "_2"] = room["id"]
    return {key: next(r for r in plynth.run("room list", "--floor", floor)["rooms"] if r["id"] == value)
            for key, value in found.items()}


# Returns true when the point lies inside the box of the room.
def near_room(plynth, floor, room, x, y):
    left, top, right, bottom = room_box(plynth, floor, room)[0]
    return left < x < right and top < y < bottom


# Returns the asset sizes of the library.
def asset_sizes(plynth):
    sizes = {}
    for asset in ("bed", "chair", "picture", "shelf", "sofa", "table", "tv", "vase"):
        sizes[asset] = plynth.run("assets info", asset, library=True, project=False)["size"]
    return sizes


# Gives the first outside door of a floor the sample door from the catalog.
def catalog_door(plynth, floor):
    walls = {w["id"]: w for w in plynth.run("wall list", "--floor", floor)["walls"]}
    for opening in plynth.run("opening list", "--floor", floor)["openings"]:
        if opening["type"] == "door" and len(walls[opening["wall"]]["rooms"]) == 1:
            plynth.run("opening set", opening["id"], "--frame", "door", library=True)
            return opening["id"]
    raise SystemExit("No outside door on %s." % floor)


# Makes a new project folder from a plan image at 100 pixels per metre.
def new_project(plynth, folder, name, image):
    plynth.project = folder
    plynth.run("project new", "--image", os.path.join(PLANS, image), "--name", name)
    plynth.run("scale set", "--pixels-per-metre", PIXELS_PER_METRE)


# Makes the apartment: two bedrooms, a bathroom, a kitchen and a living room on one floor.
def make_apartment(plynth, folder, sizes):
    new_project(plynth, folder, "Apartment", "apartment-01.png")
    rooms = detect_rooms(plynth, "f1", [((7.6, 5.57), "bedroom"), ((7.6, 10.42), "bedroom"),
                                        ((12.03, 5.07), "bathroom"), ((16.42, 5.07), "kitchen"),
                                        ((13.0, 10.0), "living")])
    places = {key: Room(plynth, "f1", room, sizes) for key, room in rooms.items()}
    for key in ("bedroom", "bedroom_2"):
        places[key].against("bed", ["left", "top", "bottom"])
        places[key].against("shelf", ["right", "bottom", "top"])
        places[key].on_wall("picture", ["top", "left", "bottom"])
    places["bathroom"].on_wall("picture", ["right", "top"])
    places["kitchen"].dining()
    places["living"].lounge(["right", "top", "left"])
    places["living"].against("shelf", ["left", "top"])
    places["living"].on_wall("picture", ["bottom", "right"])
    for place in places.values():
        place.light()
    for key in ("bathroom", "kitchen"):
        plynth.run("material set", "%s.floor" % rooms[key]["id"], "grey_tile", library=True)
    catalog_door(plynth, "f1")


# Makes the house: the ground floor with a stair hall, the upper floor, a stair, a gable roof on the upper floor and
# the default flat roof on the open strip of the ground floor.
def make_house(plynth, folder, sizes):
    new_project(plynth, folder, "House", "house-01-ground.png")
    plynth.run("floor set", "--floor", "f1", "--name", "Ground floor")
    ground = detect_rooms(plynth, "f1", [((7.57, 5.57), "living"), ((7.57, 9.42), "dining"),
                                         ((12.42, 5.07), "kitchen"), ((12.42, 8.92), "hall")])
    floor = plynth.run("floor add", "--name", "Upper floor", "--image", os.path.join(PLANS, "house-01-upper.png"))["id"]
    upper = detect_rooms(plynth, floor, [((7.57, 4.37), "bedroom"), ((7.57, 8.72), "bedroom"),
                                        ((11.95, 3.57), "bathroom"), ((11.95, 7.92), "hall")])
    add_stair(plynth, Room(plynth, "f1", ground["hall"], sizes))
    plynth.run("roof set", "--floor", floor, "--at", "7.5,6", "--type", "gable", "--pitch", 35)
    down = {key: Room(plynth, "f1", room, sizes) for key, room in ground.items()}
    down["living"].lounge(["right", "bottom", "top", "left"])
    down["living"].against("shelf", ["right", "left"])
    down["living"].on_wall("picture", ["left", "bottom"])
    down["dining"].dining()
    down["kitchen"].against("shelf", ["top", "right"])
    down["hall"].on_wall("picture", ["right", "top"])
    up = {key: Room(plynth, floor, room, sizes) for key, room in upper.items()}
    for key in ("bedroom", "bedroom_2"):
        up[key].against("bed", ["left", "top", "bottom"])
        up[key].against("shelf", ["right", "bottom", "top"])
        up[key].on_wall("picture", ["left", "top", "bottom"])
    up["bathroom"].on_wall("picture", ["right", "top"])
    for place in list(down.values()) + list(up.values()):
        place.light()
    plynth.run("material set", "%s.floor" % ground["kitchen"]["id"], "grey_tile", library=True)
    plynth.run("material set", "%s.floor" % ground["hall"]["id"], "grey_tile", library=True)
    plynth.run("material set", "%s.floor" % upper["bathroom"]["id"], "grey_tile", library=True)
    catalog_door(plynth, "f1")


# Adds a stair to the hall that clears its doors and reports no problem. Each type, width, turn and direction is
# tried once to learn its footprint, which is then moved over the hall to find places worth trying.
def add_stair(plynth, hall):
    left, top, right, bottom = hall.box
    cx, cy = round((left + right) / 2, 2), round((top + bottom) / 2, 2)
    steps = [i / 25.0 for i in range(1, 25)]
    for kind in ("spiral", "u_shape", "l_shape"):
        for width in (1.0, 0.9, 0.8):
            for rotation in (0, 90, 180, 270):
                for turn in ("right", "left"):
                    options = ["--rot", rotation, "--type", kind, "--turn", turn, "--width", width]
                    probe = plynth.run("stair add", "--floor", "f1", "--x", cx, "--y", cy, *options)
                    plynth.run("stair remove", probe["id"])
                    shape = [(px - cx, py - cy) for px, py in probe["footprint"]]
                    for fx in steps:
                        for fy in steps:
                            x, y = round(left + fx * (right - left), 2), round(top + fy * (bottom - top), 2)
                            box = (min(p[0] for p in shape) + x, min(p[1] for p in shape) + y,
                                   max(p[0] for p in shape) + x, max(p[1] for p in shape) + y)
                            inside = left < box[0] and top < box[1] and box[2] < right and box[3] < bottom
                            if not inside or blocks_opening(box, hall.openings, 2.0, STAIR_CLEARANCE):
                                continue
                            stair = plynth.run("stair add", "--floor", "f1", "--x", x, "--y", y, *options)
                            if not stair["problems"]:
                                return stair
                            plynth.run("stair remove", stair["id"])
    raise SystemExit("No stair fits the hall.")


# Copies the project files and the plan images of a scratch project into samples/projects/<id>.
def copy_project(source, target):
    if os.path.isdir(target):
        shutil.rmtree(target)
    os.makedirs(os.path.join(target, "plans"))
    for name in PROJECT_FILES:
        if os.path.isfile(os.path.join(source, name)):
            shutil.copyfile(os.path.join(source, name), os.path.join(target, name))
    for name in sorted(os.listdir(os.path.join(source, "plans"))):
        shutil.copyfile(os.path.join(source, "plans", name), os.path.join(target, "plans", name))
    for name in ("project.json", "plan.json"):
        text = open(os.path.join(target, name), encoding="utf-8").read()
        if re.search(r"[A-Za-z]:[\\/]|\\\\", text) or source.replace("\\", "/") in text.replace("\\", "/"):
            raise SystemExit("%s holds a local path." % name)


# Writes the projects list of samples.json, one project on each line like the other lists.
def write_index(projects):
    path = os.path.join(SAMPLES, "samples.json")
    text = open(path, encoding="utf-8").read()
    lines = ["    " + json.dumps(p, ensure_ascii=False) for p in projects]
    listing = '"projects": [\n' + ",\n".join(lines) + "\n  ]" if lines else '"projects": []'
    text = re.sub(r'"projects": \[.*?\]\n\}', lambda _: listing + "\n}", text, flags=re.S)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


# Makes both sample projects in the scratch folder and copies them into samples/projects.
def main():
    parser = argparse.ArgumentParser(description="Makes the sample projects with plynth commands.")
    parser.add_argument("--plynth", required=True, help="the plynth command line program")
    parser.add_argument("--scratch", required=True, help="an empty folder to work in")
    parser.add_argument("--library", help="a library that already has the sample items")
    args = parser.parse_args()
    scratch = os.path.abspath(args.scratch)
    os.makedirs(scratch, exist_ok=True)
    plynth = Plynth(args.plynth, args.library or os.path.join(scratch, "library"))
    if not args.library:
        plynth.run("library init", plynth.library, project=False)
        plynth.run("samples add", library=True, project=False)
    sizes = asset_sizes(plynth)
    entries = []
    for project, make in zip(PROJECTS, (make_apartment, make_house)):
        folder = os.path.join(scratch, project["id"])
        if os.path.exists(folder):
            raise SystemExit("%s already exists. Use an empty scratch folder." % folder)
        print("Making %s" % project["id"])
        make(plynth, folder, sizes)
        plynth.run("export licenses", "on")
        problems = plynth.run("build", "--check", library=True)
        print("  build check: %s" % json.dumps(problems.get("problems", problems))[:400])
        copy_project(folder, os.path.join(SAMPLES, "projects", project["id"]))
        entries.append({"id": project["id"], "name": project["name"], "description": project["description"],
                        "folder": "projects/%s" % project["id"],
                        "image": "projects/%s/plans/%s" % (project["id"], project["image"])})
    write_index(entries)
    print("Wrote samples/projects and samples.json.")


if __name__ == "__main__":
    sys.exit(main())
