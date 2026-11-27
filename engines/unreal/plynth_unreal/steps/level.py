import contextlib

import unreal

from .. import ue
from ..errors import LinkError
from ..registry import step


# Gives every mesh of the job one actor in the open level with the building's tag, at the origin the first
# time and kept where the user moved it after that, and removes this building's actors whose mesh is gone.
# The level is not saved.
@step("level", 100, "level_failed")
def run(ctx):
    level = ctx.job["level"]
    tag = level["tag"]
    wanted = {ctx.mesh_path(mesh["asset"]): mesh for mesh in ctx.job["meshes"]}
    with transaction("Plynth: update %s" % ctx.building):
        ours = [actor for actor in ue.actors().get_all_level_actors() if has_tag(actor, tag)]
        remaining = []
        for actor in ours:
            path = actor_mesh_path(actor)
            if path in wanted:
                remaining.append(actor)
                continue
            label = actor.get_actor_label()
            ue.actors().destroy_actor(actor)
            ctx.result.actor(label, path or "", "removed")
        if not level["place_actors"]:
            return
        for path, mesh in wanted.items():
            actor = find_actor(remaining, mesh["actor"], path)
            asset = ue.load(path)
            if asset is None:
                raise LinkError("level_failed", "The mesh %s is missing, so it could not be placed." % path)
            if actor is not None:
                remaining.remove(actor)
                if actor_mesh_path(actor) != path:
                    ue.try_get(actor, "static_mesh_component").set_static_mesh(asset)
                ctx.result.actor(actor.get_actor_label(), path, "kept")
            else:
                actor = spawn(ctx, asset, mesh["actor"], tag)
                ctx.result.actor(actor.get_actor_label(), path, "spawned")


# Wraps level changes in one undo step when the engine offers it.
@contextlib.contextmanager
def transaction(description):
    scoped = getattr(unreal, "ScopedEditorTransaction", None)
    if scoped is None:
        yield
        return
    with scoped(description):
        yield


# Returns True when an actor carries the tag.
def has_tag(actor, tag):
    try:
        return any(str(item) == tag for item in actor.tags)
    except Exception:
        return False


# Returns the package path of the static mesh an actor shows, or None.
def actor_mesh_path(actor):
    component = ue.try_get(actor, "static_mesh_component")
    if component is None:
        return None
    mesh = ue.try_get(component, "static_mesh")
    if mesh is None:
        return None
    return ue.package_path(mesh.get_path_name())


# Returns this building's actor for a mesh: the one with its label, else the one showing its mesh.
def find_actor(actors, label, path):
    for actor in actors:
        if actor.get_actor_label() == label:
            return actor
    for actor in actors:
        if actor_mesh_path(actor) == path:
            return actor
    return None


# Places a new actor for a mesh at the origin with the label, the tag and an outliner folder for the building.
def spawn(ctx, asset, label, tag):
    actor = ue.actors().spawn_actor_from_object(asset, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
    if actor is None:
        raise LinkError("level_failed", "Unreal Engine could not place %s in the level." % label)
    actor.set_actor_label(label)
    actor.set_editor_property("tags", [unreal.Name(tag)])
    try:
        actor.set_folder_path(unreal.Name("Plynth/" + ctx.building))
    except Exception as error:
        ue.warn("could not put %s in an outliner folder: %s" % (label, error))
    return actor
