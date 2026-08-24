bl_info = {
    "name": "Animation Transfer (AeroBoar)",
    "author": "Nols / AeroBoar",
    "version": (1, 3, 0),
    "blender": (4, 2, 0),
    "location": "View3D > Sidebar > AeroBoar / Properties > Object",
    "description": (
        "Copy armature actions from a source character onto a destination with "
        "matching bone names. Optionally scale location keys to dest rest size "
        "and delete the source."
    ),
    "category": "Animation",
}

import re
from typing import Optional

import bpy
from bpy.props import (
    BoolProperty,
    EnumProperty,
    FloatProperty,
    PointerProperty,
)
from bpy.types import (
    Operator,
    Panel,
    PropertyGroup,
)


_BONE_PATH = re.compile(r'^pose\.bones\["([^"]+)"\]')


def iter_action_fcurves(action):
    """Yield F-Curves on an Action (Blender 4.3 `action.fcurves` and 4.4+/5.0 slots).

    4.4 layered Actions store curves on
    `action.layers[0].strips[0].channelbag(slot).fcurves`. Blender 5.0 removed
    the legacy `Action.fcurves` proxy entirely.
    """
    if action is None:
        return
    fcurves = getattr(action, "fcurves", None)
    if fcurves is not None:
        for fc in fcurves:
            yield fc
        return

    layers = getattr(action, "layers", None)
    if not layers:
        return
    for layer in layers:
        for strip in getattr(layer, "strips", []) or []:
            bags = getattr(strip, "channelbags", None)
            if bags:
                for bag in bags:
                    for fc in bag.fcurves:
                        yield fc
                continue
            chfn = getattr(strip, "channelbag", None)
            slots = getattr(action, "slots", None) or []
            if not chfn or not slots:
                continue
            for slot in slots:
                bag = None
                try:
                    bag = chfn(slot)
                except TypeError:
                    try:
                        bag = chfn(slot, ensure=False)
                    except Exception:
                        bag = None
                except Exception:
                    bag = None
                if bag is None:
                    continue
                for fc in bag.fcurves:
                    yield fc


def first_object_slot(action):
    """First Action slot suitable for an Object/Armature (Blender 4.4+)."""
    slots = getattr(action, "slots", None)
    if not slots:
        return None
    for slot in slots:
        tid = getattr(slot, "target_id_type", None)
        if tid in ("OBJECT", "OB", None, "", "UNSPECIFIED"):
            return slot
    return slots[0] if len(slots) else None


def bind_action(holder, action):
    """Set `.action` and, on 4.4+, `.action_slot` (AnimData or NlaStrip)."""
    holder.action = action
    if action is None or not hasattr(holder, "action_slot"):
        return
    slot = first_object_slot(action)
    if slot is None:
        return
    try:
        holder.action_slot = slot
    except Exception:
        pass


# ---------------------------------------------------------------------------
# Armature / hierarchy helpers
# ---------------------------------------------------------------------------

def _name_key(name: str) -> str:
    return name.strip().lower().replace(" ", "_")


def resolve_armature(obj) -> Optional[bpy.types.Object]:
    """Armature from an armature, a skinned mesh, or a character empty."""
    if obj is None:
        return None
    if obj.type == "ARMATURE":
        return obj
    for mod in getattr(obj, "modifiers", []):
        if mod.type == "ARMATURE" and mod.object:
            return mod.object
    for child in getattr(obj, "children_recursive", []):
        if child.type == "ARMATURE":
            return child
        for mod in getattr(child, "modifiers", []):
            if mod.type == "ARMATURE" and mod.object:
                return mod.object
    return None


def character_root(obj) -> Optional[bpy.types.Object]:
    """Top-most object in the character hierarchy (empty or armature)."""
    if obj is None:
        return None
    cur = obj
    while cur.parent is not None:
        cur = cur.parent
    return cur


def hierarchy_objects(root) -> list:
    if root is None:
        return []
    return [root] + list(root.children_recursive)


def _unique_armatures(objects) -> list:
    arms = []
    for o in objects:
        arm = resolve_armature(o)
        if arm is not None and arm not in arms:
            arms.append(arm)
    return arms


def detect_source_dest(context):
    """Return (source_arm, dest_arm). Active object is destination when in the pair.

    Uses exactly two unique armatures from the selection, or from the scene if
    the scene has exactly two. Does not guess when more than two exist.
    """
    selected = [o for o in context.selected_objects if o]
    sel_arms = _unique_armatures(selected)

    if len(sel_arms) == 2:
        arms = sel_arms
    elif len(sel_arms) > 2:
        return None, None
    else:
        scene_arms = _unique_armatures(context.scene.objects)
        if len(scene_arms) != 2:
            return None, None
        arms = scene_arms

    active_arm = resolve_armature(context.active_object)
    dest = active_arm if active_arm in arms else arms[-1]
    source = next(a for a in arms if a != dest)
    return source, dest


# ---------------------------------------------------------------------------
# Bones / actions
# ---------------------------------------------------------------------------

def bone_name_map(src_arm, dst_arm) -> dict:
    """src bone name → dst bone name (exact, then case-insensitive)."""
    dst_names = {b.name: b.name for b in dst_arm.data.bones}
    dst_lower = {_name_key(b.name): b.name for b in dst_arm.data.bones}
    mapping = {}
    for src in src_arm.data.bones:
        if src.name in dst_names:
            mapping[src.name] = dst_names[src.name]
        elif _name_key(src.name) in dst_lower:
            mapping[src.name] = dst_lower[_name_key(src.name)]
    return mapping


def _fcurve_bone_name(data_path: str) -> Optional[str]:
    m = _BONE_PATH.match(data_path or "")
    return m.group(1) if m else None


def action_targets_armature(action, arm) -> bool:
    bones = {b.name for b in arm.data.bones}
    bones_l = {_name_key(n) for n in bones}
    hit = False
    for fc in iter_action_fcurves(action):
        bn = _fcurve_bone_name(fc.data_path)
        if bn is None:
            continue
        if bn in bones or _name_key(bn) in bones_l:
            hit = True
            break
    return hit


def actions_used_by(arm) -> set:
    used = set()
    ad = arm.animation_data if arm else None
    if not ad:
        return used
    if ad.action:
        used.add(ad.action)
    for track in ad.nla_tracks:
        for strip in track.strips:
            if strip.action:
                used.add(strip.action)
    return used


def collect_source_actions(src_arm, include_orphans: bool, dst_arm=None) -> list:
    """Actions on the source armature (NLA + assigned), plus unused clips."""
    found = []
    seen = set()

    def add(action):
        if action is None or action.name in seen:
            return
        seen.add(action.name)
        found.append(action)

    ad = src_arm.animation_data
    if ad:
        add(ad.action)
        for track in ad.nla_tracks:
            for strip in track.strips:
                add(strip.action)

    dest_used = actions_used_by(dst_arm) if dst_arm else set()
    if include_orphans:
        for action in bpy.data.actions:
            if action.name in seen:
                continue
            if action in dest_used:
                continue
            if action_targets_armature(action, src_arm):
                add(action)
    return found


def _find_bone(arm, *names):
    bones = arm.data.bones
    for n in names:
        b = bones.get(n)
        if b is not None:
            return b
    lower = {b.name.lower(): b for b in bones}
    for n in names:
        b = lower.get(n.lower())
        if b is not None:
            return b
    return None


def _bone_tail_key(name: str) -> str:
    """Last segment after Mixamo-style prefixes (`mixamorig:Hips` → `hips`)."""
    k = _name_key(name)
    if ":" in k:
        k = k.rsplit(":", 1)[-1]
    return k


def _is_hip_bone_name(name: str) -> bool:
    return _bone_tail_key(name) in ("hips", "pelvis", "hip")


def _is_root_bone_name(name: str) -> bool:
    return _bone_tail_key(name) in ("root", "armature")


def rest_hip_height(arm) -> float:
    """World-space height of rest-pose hips above the armature object origin.

    Captures object scale and Apply Scale (smaller rest bones). Blender is Z-up.
    """
    if arm is None:
        return 0.0
    bone = _find_bone(arm, "hips", "Hips", "pelvis", "Pelvis")
    if bone is None:
        for b in arm.data.bones:
            if _is_hip_bone_name(b.name):
                bone = b
                break
    origin = arm.matrix_world.translation
    if bone is None:
        s = arm.matrix_world.to_scale()
        return (abs(s.x) + abs(s.y) + abs(s.z)) / 3.0
    head = arm.matrix_world @ bone.head_local
    return float(head.z - origin.z)


def auto_location_scale(src_arm, dst_arm) -> float:
    src_h = rest_hip_height(src_arm)
    dst_h = rest_hip_height(dst_arm)
    if abs(src_h) < 1e-8:
        return 1.0
    return dst_h / src_h


def effective_location_scale(src_arm, dst_arm, settings) -> float:
    override = float(getattr(settings, "location_scale", 0.0) or 0.0)
    if abs(override) > 1e-8:
        return override
    return auto_location_scale(src_arm, dst_arm)


def scale_action_pose_locations(action, factor: float) -> int:
    """Multiply pose-bone location keys (and bezier handles) by factor.

    Rotations/scales are left alone. Returns number of F-curves touched.
    """
    if action is None or abs(factor - 1.0) < 1e-6:
        return 0
    touched = 0
    for fc in iter_action_fcurves(action):
        path = fc.data_path or ""
        if not path.endswith(".location") and path != "location":
            continue
        if _fcurve_bone_name(path) is None:
            continue
        for kp in fc.keyframe_points:
            kp.co[1] *= factor
            kp.handle_left[1] *= factor
            kp.handle_right[1] *= factor
        if hasattr(fc, "update"):
            fc.update()
        touched += 1
    return touched


def dest_clip_actions(arm) -> list:
    """Assigned action + NLA strip actions on dest (unique, stable order)."""
    found = []
    seen = set()

    def add(action):
        if action is None:
            return
        try:
            name = action.name
        except ReferenceError:
            return
        if name in seen:
            return
        seen.add(name)
        found.append(action)

    ad = arm.animation_data if arm else None
    if not ad:
        return found
    add(ad.action)
    for track in ad.nla_tracks:
        for strip in track.strips:
            add(strip.action)
    return found


def remap_action_bone_paths(action, name_map: dict) -> int:
    """Rewrite pose.bones[\"src\"] paths to dest names. Returns curves changed."""
    changed = 0
    for fc in iter_action_fcurves(action):
        bn = _fcurve_bone_name(fc.data_path)
        if bn is None or bn not in name_map:
            continue
        dst = name_map[bn]
        if dst == bn:
            continue
        fc.data_path = fc.data_path.replace(
            f'pose.bones["{bn}"]', f'pose.bones["{dst}"]', 1
        )
        changed += 1
    return changed


def install_action_copy(src_action, desired_name: str, replace: bool):
    """Duplicate an action. Never deletes src_action (Blender 5 invalidates the RNA).

    The copy may be named Clip.001 until the source is gone; reclaim later.
    """
    new_act = src_action.copy()
    existing = bpy.data.actions.get(desired_name)
    if existing is not None and existing != new_act and existing != src_action:
        if replace:
            existing.user_remap(new_act)
            bpy.data.actions.remove(existing)
        else:
            bpy.data.actions.remove(new_act)
            return existing
    taken = bpy.data.actions.get(desired_name)
    if taken is None or taken == new_act:
        try:
            new_act.name = desired_name
        except Exception:
            pass
    new_act.use_fake_user = True
    return new_act


def reclaim_action_names(pairs: list):
    """Rename dest copies to the original clip names after the source is deleted."""
    for act, desired in pairs:
        if act is None or desired is None:
            continue
        try:
            act.name
        except ReferenceError:
            continue
        if act.name == desired:
            continue
        other = bpy.data.actions.get(desired)
        if other is not None and other != act:
            try:
                bpy.data.actions.remove(other, do_unlink=True)
            except Exception:
                continue
        try:
            act.name = desired
        except Exception:
            pass


def clear_nla_track_named(arm, name: str):
    ad = arm.animation_data
    if not ad:
        return
    for track in list(ad.nla_tracks):
        if track.name == name:
            ad.nla_tracks.remove(track)


def stash_nla(arm, action, mute: bool = True, track_name: str = None):
    """One NLA track per clip so Khronos glTF export emits separate animations."""
    name = track_name or action.name
    ad = arm.animation_data_create()
    clear_nla_track_named(arm, name)
    track = ad.nla_tracks.new()
    track.name = name
    start = int(action.frame_range[0])
    strip = track.strips.new(name, start, action)
    bind_action(strip, action)
    track.mute = mute
    return track


def frame_range_of(action) -> tuple:
    fr = action.frame_range
    a, b = int(fr[0]), int(fr[1])
    if b <= a:
        b = a + 1
    return a, b


# ---------------------------------------------------------------------------
# Transfer
# ---------------------------------------------------------------------------

def copy_actions_to_dest(src_arm, dst_arm, settings, report) -> tuple:
    """Duplicate source actions onto dest, remap bones, stash NLA. Returns counts."""
    name_map = bone_name_map(src_arm, dst_arm)
    if not name_map:
        report({"ERROR"}, "No matching bone names between source and destination.")
        return 0, 0, name_map, []

    actions = collect_source_actions(
        src_arm, settings.include_unused_actions, dst_arm
    )
    if not actions:
        report({"ERROR"}, "Source armature has no actions (NLA / assigned / unused).")
        return 0, 0, name_map, []

    transferred = 0
    skipped = 0
    preview_action = None
    prefer_preview = {
        "t-pose",
        "tpose",
        "idle",
        "walk",
        "run",
        "walking_a",
        "running_a",
    }
    copied = []
    for src_act in actions:
        src_name = src_act.name
        if not settings.replace_existing:
            ad = dst_arm.animation_data
            if ad and any(t.name == src_name for t in ad.nla_tracks):
                skipped += 1
                continue

        new_act = install_action_copy(
            src_act, src_name, replace=settings.replace_existing
        )
        remap_action_bone_paths(new_act, name_map)
        if settings.scale_location:
            fac = effective_location_scale(src_arm, dst_arm, settings)
            scale_action_pose_locations(new_act, fac)
        stash_nla(dst_arm, new_act, mute=True, track_name=src_name)
        transferred += 1
        copied.append((new_act, src_name))
        if preview_action is None or src_name.lower() in prefer_preview:
            preview_action = new_act

    ad = dst_arm.animation_data_create()
    if preview_action is not None:
        bind_action(ad, preview_action)
    return transferred, skipped, name_map, copied


def _add_copy_constraints(src_arm, dst_arm, name_map, copy_location: bool):
    """Pose-bone Copy Rotation (and optional Location) from source → dest."""
    for src_name, dst_name in name_map.items():
        pbone = dst_arm.pose.bones.get(dst_name)
        if pbone is None:
            continue
        # Clear leftover copy constraints from a previous failed run.
        for c in list(pbone.constraints):
            if c.name.startswith("AnimXfer_"):
                pbone.constraints.remove(c)
        crc = pbone.constraints.new("COPY_ROTATION")
        crc.name = "AnimXfer_Rot"
        crc.target = src_arm
        crc.subtarget = src_name
        crc.target_space = "WORLD"
        crc.owner_space = "WORLD"
        crc.mix_mode = "REPLACE"
        if copy_location and (
            _is_root_bone_name(dst_name) or _is_hip_bone_name(dst_name)
        ):
            clc = pbone.constraints.new("COPY_LOCATION")
            clc.name = "AnimXfer_Loc"
            clc.target = src_arm
            clc.subtarget = src_name
            clc.target_space = "WORLD"
            clc.owner_space = "WORLD"


def _clear_copy_constraints(dst_arm):
    for pbone in dst_arm.pose.bones:
        for c in list(pbone.constraints):
            if c.name.startswith("AnimXfer_"):
                pbone.constraints.remove(c)


def bake_actions_to_dest(context, src_arm, dst_arm, settings, report) -> tuple:
    """Move source onto dest, constrain, bake each clip, restore dest transform."""
    name_map = bone_name_map(src_arm, dst_arm)
    if not name_map:
        report({"ERROR"}, "No matching bone names between source and destination.")
        return 0, 0, name_map, []

    actions = collect_source_actions(
        src_arm, settings.include_unused_actions, dst_arm
    )
    if not actions:
        report({"ERROR"}, "Source armature has no actions to bake.")
        return 0, 0, name_map, []

    dest_mx = dst_arm.matrix_world.copy()
    src_arm.matrix_world = dest_mx.copy()

    src_ad = src_arm.animation_data_create()
    transferred = 0
    skipped = 0
    copied = []

    view_layer = context.view_layer
    for src_act in actions:
        src_name = src_act.name
        if not settings.replace_existing:
            ad = dst_arm.animation_data
            if ad and any(t.name == src_name for t in ad.nla_tracks):
                skipped += 1
                continue

        bind_action(src_ad, src_act)
        f0, f1 = frame_range_of(src_act)
        context.scene.frame_start = f0
        context.scene.frame_end = f1
        context.scene.frame_set(f0)

        _clear_copy_constraints(dst_arm)
        _add_copy_constraints(
            src_arm, dst_arm, name_map, copy_location=settings.bake_root_location
        )

        for o in list(context.selected_objects):
            o.select_set(False)
        dst_arm.select_set(True)
        view_layer.objects.active = dst_arm

        bpy.ops.object.mode_set(mode="POSE")
        bpy.ops.pose.select_all(action="SELECT")
        dst_arm.animation_data_create()
        bind_action(dst_arm.animation_data, None)
        bpy.ops.nla.bake(
            frame_start=f0,
            frame_end=f1,
            step=1,
            only_selected=True,
            visual_keying=True,
            clear_constraints=False,
            clear_parents=False,
            use_current_action=False,
            bake_types={"POSE"},
        )
        bpy.ops.object.mode_set(mode="OBJECT")

        baked = dst_arm.animation_data.action if dst_arm.animation_data else None
        if baked is None:
            report({"WARNING"}, f"Bake produced no action for '{src_name}'")
            continue
        baked.use_fake_user = True
        existing = bpy.data.actions.get(src_name)
        if (
            existing is not None
            and existing != baked
            and existing != src_act
            and settings.replace_existing
        ):
            existing.user_remap(baked)
            try:
                bpy.data.actions.remove(existing)
            except Exception:
                pass
        stash_nla(dst_arm, baked, mute=True, track_name=src_name)
        copied.append((baked, src_name))
        transferred += 1

    _clear_copy_constraints(dst_arm)
    dst_arm.matrix_world = dest_mx
    return transferred, skipped, name_map, copied


def delete_source_character(src_arm, dst_arm, report) -> int:
    """Unlink and remove the source character (armature + meshes + empty)."""
    src_root = character_root(src_arm)
    dst_root = character_root(dst_arm)
    if src_root is None:
        return 0
    if dst_root is not None and (
        src_root == dst_root or dst_root in hierarchy_objects(src_root)
    ):
        report(
            {"ERROR"},
            "Source and destination share a hierarchy — refusing to delete.",
        )
        return 0

    victims = [
        o
        for o in hierarchy_objects(src_root)
        if o.name in bpy.data.objects
    ]
    # Armature last so modifiers drop cleanly.
    victims.sort(key=lambda o: 0 if o.type != "ARMATURE" else 1)
    removed = 0
    for obj in victims:
        try:
            bpy.data.objects.remove(obj, do_unlink=True)
            removed += 1
        except Exception:
            pass
    return removed


# ---------------------------------------------------------------------------
# Scene settings
# ---------------------------------------------------------------------------

class AnimXferSettings(PropertyGroup):
    source: PointerProperty(
        name="Source",
        type=bpy.types.Object,
        description="Armature (or skinned mesh / empty) that already has the clips",
    )
    dest: PointerProperty(
        name="Destination",
        type=bpy.types.Object,
        description="Armature (or skinned mesh / empty) that should receive the clips",
    )
    mode: EnumProperty(
        name="Mode",
        items=(
            (
                "COPY",
                "Duplicate Actions",
                "Copy method: duplicate Action data-blocks onto the destination "
                "(same bone names). Does not run until Transfer Animations.",
            ),
            (
                "BAKE",
                "Move + Constrain + Bake",
                "Snap source onto dest, Copy Rotation constraints, bake visual keys, "
                "then delete source. Use if rest poses differ.",
            ),
        ),
        default="COPY",
    )
    include_unused_actions: BoolProperty(
        name="Include unused clips",
        description=(
            "Also copy Actions in the .blend that target the source bones but are "
            "not on its NLA stack (common after a glTF import)"
        ),
        default=True,
    )
    replace_existing: BoolProperty(
        name="Replace clips with the same name",
        description="Overwrite destination clips that already have the same name",
        default=True,
    )
    bake_root_location: BoolProperty(
        name="Bake root/hips location",
        description="In Bake mode, also copy location of root and hips (root motion)",
        default=False,
    )
    scale_location: BoolProperty(
        name="Scale location to dest size",
        description=(
            "Multiply pose-bone location keys by dest/source rest hip height. "
            "Use when the destination is a different size than the source. "
            "Rotations are unchanged"
        ),
        default=True,
    )
    location_scale: FloatProperty(
        name="Location scale",
        description=(
            "0 = auto (dest hip height / source hip height). "
            "Set dest/src as a factor if auto is 1.0 but dest was Apply-Scaled "
            "and the source is already gone"
        ),
        default=0.0,
        min=-10.0,
        max=10.0,
        step=1,
        precision=4,
    )
    skip_tpose_on_scale: BoolProperty(
        name="Skip idle/bind clips when scaling dest",
        description=(
            "Scale dest clip locations leaves Idle / T-Pose / bind / rest "
            "alone (those clips are often already dest-sized)"
        ),
        default=True,
    )
    delete_source: BoolProperty(
        name="Delete source character",
        description="After a successful transfer, delete the source armature, meshes, and empty",
        default=True,
    )


# ---------------------------------------------------------------------------
# Operators
# ---------------------------------------------------------------------------

class ANIMXFER_OT_detect(Operator):
    bl_idname = "animxfer.detect"
    bl_label = "Detect source → dest"
    bl_description = (
        "Find two armatures from the selection (active = destination) "
        "or from the scene if exactly two exist"
    )
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        src, dst = detect_source_dest(context)
        if src is None or dst is None:
            self.report(
                {"WARNING"},
                "Need exactly two armatures. Select source then destination "
                "(destination active), or pick them in the panel.",
            )
            return {"CANCELLED"}
        settings = context.scene.animxfer_settings
        settings.source = src
        settings.dest = dst
        nmap = bone_name_map(src, dst)
        self.report(
            {"INFO"},
            f"Source='{src.name}' dest='{dst.name}'  matched bones={len(nmap)}/"
            f"{len(src.data.bones)}",
        )
        return {"FINISHED"}


class ANIMXFER_OT_transfer(Operator):
    bl_idname = "animxfer.transfer"
    bl_label = "Transfer Animations"
    bl_description = (
        "Copy all source clips onto the destination armature as NLA tracks "
        "(glTF exporter = one animation per track), then optionally delete source"
    )
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        s = getattr(context.scene, "animxfer_settings", None)
        if s is None:
            return False
        return s.source is not None and s.dest is not None and s.source != s.dest

    def execute(self, context):
        settings = context.scene.animxfer_settings
        src_arm = resolve_armature(settings.source)
        dst_arm = resolve_armature(settings.dest)
        if src_arm is None:
            self.report({"ERROR"}, "Source is not an armature / skinned character.")
            return {"CANCELLED"}
        if dst_arm is None:
            self.report({"ERROR"}, "Destination is not an armature / skinned character.")
            return {"CANCELLED"}
        if src_arm == dst_arm:
            self.report({"ERROR"}, "Source and destination resolved to the same armature.")
            return {"CANCELLED"}

        if settings.mode == "BAKE":
            transferred, skipped, nmap, copied = bake_actions_to_dest(
                context, src_arm, dst_arm, settings, self.report
            )
        else:
            transferred, skipped, nmap, copied = copy_actions_to_dest(
                src_arm, dst_arm, settings, self.report
            )

        if transferred == 0 and skipped == 0:
            return {"CANCELLED"}

        nsrc_bones = len(src_arm.data.bones)
        fac = 1.0
        if settings.mode != "BAKE" and settings.scale_location:
            fac = effective_location_scale(src_arm, dst_arm, settings)
        deleted = 0
        if settings.delete_source and (transferred > 0 or skipped > 0):
            deleted = delete_source_character(src_arm, dst_arm, self.report)
            settings.source = None
        reclaim_action_names(copied)

        extra = ""
        if settings.mode != "BAKE" and settings.scale_location:
            extra = f", loc scale={fac:.4f}"
        self.report(
            {"INFO"},
            f"Transferred {transferred} clip(s), skipped {skipped}, "
            f"bones matched {len(nmap)}/{nsrc_bones}, "
            f"deleted {deleted} source object(s){extra}.",
        )
        return {"FINISHED"}


class ANIMXFER_OT_scale_dest(Operator):
    bl_idname = "animxfer.scale_dest_locations"
    bl_label = "Scale dest clip locations"
    bl_description = (
        "Multiply location keys on clips already on the destination "
        "(NLA + assigned action). Use after a 1:1 copy, or to retry a factor"
    )
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        s = getattr(context.scene, "animxfer_settings", None)
        if s is None or s.dest is None:
            return False
        return resolve_armature(s.dest) is not None

    def execute(self, context):
        settings = context.scene.animxfer_settings
        dst_arm = resolve_armature(settings.dest)
        src_arm = resolve_armature(settings.source)
        if abs(float(settings.location_scale or 0.0)) < 1e-8 and src_arm is None:
            self.report(
                {"ERROR"},
                "Need source for auto scale, or type Location scale "
                "(dest hip height / source hip height).",
            )
            return {"CANCELLED"}
        if src_arm is None:
            fac = float(settings.location_scale)
        else:
            fac = effective_location_scale(src_arm, dst_arm, settings)
        actions = dest_clip_actions(dst_arm)
        skip = {"tpose", "idle", "bind", "rest"}
        curves = 0
        used = 0
        for act in actions:
            key = (
                act.name.lower()
                .replace(" ", "")
                .replace("_", "")
                .replace("-", "")
            )
            if settings.skip_tpose_on_scale and key in skip:
                continue
            curves += scale_action_pose_locations(act, fac)
            used += 1
        self.report(
            {"INFO"},
            f"Scaled location on {used} clip(s), {curves} F-curve(s), "
            f"factor={fac:.4f}.",
        )
        return {"FINISHED"}


# ---------------------------------------------------------------------------
# UI
# ---------------------------------------------------------------------------

def _draw_panel(layout, context):
    settings = context.scene.animxfer_settings
    col = layout.column(align=True)
    col.operator("animxfer.detect", icon="VIEWZOOM")

    col.separator()
    col.prop(settings, "source")
    col.prop(settings, "dest")

    src_arm = resolve_armature(settings.source)
    dst_arm = resolve_armature(settings.dest)
    box = layout.box()
    if src_arm and dst_arm and src_arm != dst_arm:
        nmap = bone_name_map(src_arm, dst_arm)
        nsrc = len(src_arm.data.bones)
        ndst = len(dst_arm.data.bones)
        box.label(
            text=f"Bones matched: {len(nmap)} / source {nsrc}  (dest {ndst})",
            icon="BONE_DATA",
        )
        src_acts = collect_source_actions(
            src_arm, settings.include_unused_actions, dst_arm
        )
        names = ", ".join(a.name for a in src_acts[:8])
        if len(src_acts) > 8:
            names += ", …"
        box.label(text=f"Source clips ({len(src_acts)}): {names or '(none)'}")
        missing = nsrc - len(nmap)
        if missing:
            box.label(text=f"{missing} source bone(s) have no dest match", icon="ERROR")
        src_h = rest_hip_height(src_arm)
        dst_h = rest_hip_height(dst_arm)
        auto = auto_location_scale(src_arm, dst_arm)
        fac = effective_location_scale(src_arm, dst_arm, settings)
        box.label(
            text=f"Rest hip height  src {src_h:.4f}  dest {dst_h:.4f}  (Blender Z)"
        )
        kind = "auto" if abs(float(settings.location_scale or 0.0)) < 1e-8 else "override"
        box.label(text=f"Location scale {fac:.4f} ({kind}; raw auto {auto:.4f})")
        if abs(auto - 1.0) < 0.05 and settings.scale_location:
            box.label(
                text="Heights similar — type a scale if dest is smaller",
                icon="ERROR",
            )
    else:
        box.label(text="Pick source and destination armatures.", icon="INFO")

    layout.separator()
    layout.label(text="Copy method (does not run by itself):")
    layout.prop(settings, "mode", expand=True)
    layout.prop(settings, "include_unused_actions")
    layout.prop(settings, "replace_existing")
    if settings.mode == "BAKE":
        layout.prop(settings, "bake_root_location")
    else:
        layout.prop(settings, "scale_location")
        layout.prop(settings, "location_scale")
        layout.prop(settings, "skip_tpose_on_scale")
        layout.operator("animxfer.scale_dest_locations", icon="FULLSCREEN_ENTER")
    layout.prop(settings, "delete_source")

    layout.separator()
    row = layout.row()
    row.scale_y = 1.4
    xfer_label = (
        "Transfer Animations (deletes source)"
        if settings.delete_source
        else "Transfer Animations"
    )
    row.operator("animxfer.transfer", text=xfer_label, icon="ANIM")

    layout.separator()
    helpb = layout.box()
    helpb.label(text="After transfer, scrub a loco clip — feet near origin.", icon="INFO")
    helpb.label(text="Export dest only: glTF 2.0 → NLA Tracks")
    helpb.label(text="Include → Custom Properties (ECS extras)")


class ANIMXFER_PT_view3d(Panel):
    bl_label = "Animation Transfer"
    bl_idname = "ANIMXFER_PT_view3d"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "AeroBoar"

    def draw(self, context):
        _draw_panel(self.layout, context)


class ANIMXFER_PT_object(Panel):
    bl_label = "Animation Transfer (AeroBoar)"
    bl_idname = "ANIMXFER_PT_object"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "object"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        _draw_panel(self.layout, context)


# ---------------------------------------------------------------------------
# Registration
# ---------------------------------------------------------------------------

classes = (
    AnimXferSettings,
    ANIMXFER_OT_detect,
    ANIMXFER_OT_transfer,
    ANIMXFER_OT_scale_dest,
    ANIMXFER_PT_view3d,
    ANIMXFER_PT_object,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    bpy.types.Scene.animxfer_settings = PointerProperty(type=AnimXferSettings)


def unregister():
    del bpy.types.Scene.animxfer_settings
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)


if __name__ == "__main__":
    register()
