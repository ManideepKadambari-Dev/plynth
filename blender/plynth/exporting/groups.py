KIT_KIND = "kit"


# Says whether a group without members takes the object by its kinds and ids filters (an empty list takes all).
def _filters_take(group, kind, plan_id):
    kinds = group.get("kinds") or []
    ids = group.get("ids") or []
    return (not kinds or kind in kinds) and (not ids or plan_id in ids)


# Returns the id of the group that takes one object, or None. The order is the one in Exporter.h: the exact
# member, the kind member (null id), the first group without members whose filters take it, the rest group.
def _group_of(groups, kind, plan_id):
    for group in groups:
        if any(m["kind"] == kind and m["id"] == plan_id and plan_id is not None for m in group.get("members") or []):
            return group["id"]
    for group in groups:
        if any(m["kind"] == kind and m["id"] is None for m in group.get("members") or []):
            return group["id"]
    for group in groups:
        if not group.get("members") and _filters_take(group, kind, plan_id):
            return group["id"]
    for group in groups:
        if group.get("rest"):
            return group["id"]
    return None


# Sorts building objects into groups. meshes are the building meshes, collision the UCX_ meshes. Returns
# (objects by group id, objects no group takes). Every group id is a key, an empty list when it got nothing. A
# collision mesh goes with the object it belongs to; one with no such object goes where the objects of no kind
# would go. Objects of kind "kit" are left out of both answers.
def assign(groups, meshes, collision=()):
    found = {group["id"]: [] for group in groups}
    unassigned = []
    owner = {}
    for obj in meshes:
        kind = obj.get("plynth_kind")
        if kind == KIT_KIND:
            continue
        target = _group_of(groups, kind, obj.get("plynth_id") or None)
        owner[obj.name] = target
        if target is None:
            unassigned.append(obj)
        else:
            found[target].append(obj)
    for obj in collision:
        if obj.get("plynth_kind") == KIT_KIND:
            continue
        name = obj.get("plynth_collision_for")
        target = owner[name] if name in owner else _group_of(groups, None, None)
        if name in owner and target is None:
            continue
        if target is None:
            unassigned.append(obj)
        else:
            found[target].append(obj)
    for objects in found.values():
        objects.sort(key=lambda o: o.name)
    return found, unassigned
