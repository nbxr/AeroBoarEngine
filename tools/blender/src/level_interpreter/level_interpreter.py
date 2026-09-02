bl_info = {
    "name": "Level Interpreter (AeroBoar)",
    "author": "Nols / AeroBoar",
    "version": (1, 1, 0),
    "blender": (4, 2, 0),
    "location": "View3D > Sidebar > AeroBoar / File > Import",
    "description": (
        "Import LevelToJsonMap JSON (e.g. smb1-1.json) as placeholder meshes "
        "with KHR_physics_rigid_bodies box colliders."
    ),
    "category": "Import-Export",
}

import json
from pathlib import Path

import bpy
import bmesh
from bpy.props import (
    StringProperty,
    FloatProperty,
    BoolProperty,
    PointerProperty,
)
from bpy.types import Operator, Panel, PropertyGroup


# JSON (x,y) is tile bottom-left. Blender: X along the run, Z up, Y = thickness.
# glTF export converts Blender Z-up → glTF/engine Y-up.
_TILE_DEFAULT = 1.0
_THICK_DEFAULT = 1.0

_COLORS = {
    "ground": (0.45, 0.28, 0.12, 1.0),
    "bricks": (0.78, 0.38, 0.16, 1.0),
    "question": (0.95, 0.78, 0.12, 1.0),
    "pipe": (0.12, 0.52, 0.18, 1.0),
    "pipe_end": (0.18, 0.68, 0.22, 1.0),
    "stairs": (0.55, 0.55, 0.58, 1.0),
    "castle": (0.42, 0.40, 0.38, 1.0),
    "flag_pole": (0.82, 0.82, 0.80, 1.0),
    "flag_end": (0.85, 0.12, 0.10, 1.0),
}

_CONTENTS_TINT = {
    "coins": (1.0, 0.85, 0.2, 1.0),
    "mushroom": (0.9, 0.2, 0.15, 1.0),
    "star": (1.0, 1.0, 0.35, 1.0),
}


def _collection_name(stem: str) -> str:
    safe = "".join(c if c.isalnum() or c in "-_" else "_" for c in stem)
    return f"Level_{safe}"


def _ensure_material(name: str, color):
    mat = bpy.data.materials.get(name)
    if mat is None:
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
        nt = mat.node_tree
        bsdf = nt.nodes.get("Principled BSDF")
        if bsdf:
            bsdf.inputs["Base Color"].default_value = color
            if "Roughness" in bsdf.inputs:
                bsdf.inputs["Roughness"].default_value = 0.7
    mat.diffuse_color = color
    return mat


def _shared_mesh(key: str, builder):
    name = f"LevelPH_{key}"
    mesh = bpy.data.meshes.get(name)
    if mesh is not None:
        return mesh
    mesh = builder(name)
    return mesh


def _build_unit_cube(name: str):
    mesh = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bm.to_mesh(mesh)
    bm.free()
    mesh.update()
    return mesh


def _build_cylinder_z(name: str, segments: int = 16):
    # bmesh cone is already along +Z (Blender up).
    mesh = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_cone(
        bm,
        cap_ends=True,
        cap_tris=False,
        segments=segments,
        radius1=0.5,
        radius2=0.5,
        depth=1.0,
    )
    bm.to_mesh(mesh)
    bm.free()
    mesh.update()
    return mesh


def _build_sphere(name: str, segments: int = 12):
    mesh = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(
        bm, u_segments=segments, v_segments=max(6, segments // 2), radius=0.5
    )
    bm.to_mesh(mesh)
    bm.free()
    mesh.update()
    return mesh


def _set_khr_box(obj):
    """Unit-local box; object scale is the full extents (engine × node scale)."""
    payload = {
        "collider": {
            "geometry": {
                "box": {"size": [1.0, 1.0, 1.0]},
            }
        }
    }
    obj["KHR_physics_rigid_bodies"] = json.dumps(payload, separators=(",", ":"))


def _link_object(obj, coll, parent):
    coll.objects.link(obj)
    obj.parent = parent


def _clear_collection(coll):
    for obj in list(coll.objects):
        mesh = obj.data if obj.type == "MESH" else None
        bpy.data.objects.remove(obj, do_unlink=True)
        if mesh is not None and mesh.users == 0:
            bpy.data.meshes.remove(mesh)


def _object_thickness(spec, default: float) -> float:
    """JSON thickness is in tiles; omit / empty → default (1.0 unless the panel overrides)."""
    raw = spec.get("thickness", None)
    if raw is None or raw == "":
        return float(default)
    try:
        return float(raw)
    except (TypeError, ValueError):
        return float(default)


def import_level_json(filepath: str, tile: float, thickness: float, replace: bool):
    path = Path(filepath)
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)

    objects = data.get("level_objects") or []
    coll_name = _collection_name(path.stem)

    if coll_name in bpy.data.collections and replace:
        _clear_collection(bpy.data.collections[coll_name])
        coll = bpy.data.collections[coll_name]
    elif coll_name in bpy.data.collections:
        n = 1
        while f"{coll_name}_{n:03d}" in bpy.data.collections:
            n += 1
        coll_name = f"{coll_name}_{n:03d}"
        coll = bpy.data.collections.new(coll_name)
        bpy.context.scene.collection.children.link(coll)
    else:
        coll = bpy.data.collections.new(coll_name)
        bpy.context.scene.collection.children.link(coll)

    root = bpy.data.objects.new(f"{coll_name}_root", None)
    root.empty_display_type = "PLAIN_AXES"
    root.empty_display_size = tile
    _link_object(root, coll, None)
    root["level_json"] = str(path)
    root["level_width"] = int(data.get("width") or 0)
    root["level_height"] = int(data.get("height") or 0)
    root["tile_size"] = float(tile)
    root["thickness"] = float(thickness)

    counts = {}
    for spec in objects:
        typ = str(spec.get("type") or "unknown")
        contents = str(spec.get("contents") or "")
        tx = float(spec.get("x") or 0)
        ty = float(spec.get("y") or 0)
        tw = max(float(spec.get("width") or 1), 1.0)
        th = max(float(spec.get("height") or 1), 1.0)
        obj_thick = _object_thickness(spec, thickness)

        sx = tw * tile
        sz = th * tile
        sy = obj_thick * tile
        # Pipe lip: slightly larger in X (run) and Y (depth) than the shaft.
        if typ == "pipe_end":
            sx = max(sx, 2.0 * tile) * 1.15
            sy = sy * 1.15
        if typ == "flag_pole":
            sx = 0.18 * tile
        if typ == "flag_end":
            sx = 0.7 * tile
            sy = 0.7 * tile
            sz = 0.7 * tile

        cx = (tx + tw * 0.5) * tile
        cz = (ty + th * 0.5) * tile
        cy = 0.0

        if typ in ("pipe", "pipe_end", "flag_pole"):
            mesh = _shared_mesh("cyl_z", _build_cylinder_z)
        elif typ == "flag_end":
            mesh = _shared_mesh("sph", _build_sphere)
        else:
            mesh = _shared_mesh("cube", _build_unit_cube)

        name = f"{typ}_{int(tx)}_{int(ty)}"
        if contents:
            name += f"_{contents}"
        obj = bpy.data.objects.new(name, mesh)
        obj.location = (cx, cy, cz)
        obj.scale = (sx, sy, sz)
        color = _CONTENTS_TINT.get(contents, _COLORS.get(typ, (0.6, 0.6, 0.6, 1.0)))
        mat_name = f"Level_{typ}" + (f"_{contents}" if contents else "")
        obj.data.materials.append(_ensure_material(mat_name, color))
        obj["level_type"] = typ
        obj["level_contents"] = contents
        obj["level_tile"] = [int(tx), int(ty), int(tw), int(th)]
        obj["level_thickness"] = obj_thick
        _set_khr_box(obj)
        _link_object(obj, coll, root)
        counts[typ] = counts.get(typ, 0) + 1

    return coll_name, len(objects), counts


class LevelInterpreterSettings(PropertyGroup):
    filepath: StringProperty(
        name="JSON",
        description="LevelToJsonMap file (width/height + level_objects[])",
        default="",
        subtype="FILE_PATH",
    )
    tile_size: FloatProperty(
        name="Tile size",
        description="Meters per JSON tile (before engine worldScale)",
        default=_TILE_DEFAULT,
        min=0.01,
        max=10.0,
    )
    thickness: FloatProperty(
        name="Default thickness",
        description=(
            "Tiles of slab depth (Blender Y) when an object omits JSON thickness. "
            "JSON thickness × tile size; typical: ground/pipes 2, blocks/flagpoles 1"
        ),
        default=_THICK_DEFAULT,
        min=0.05,
        max=10.0,
    )
    replace: BoolProperty(
        name="Replace collection",
        description="Rebuild Level_<stem> instead of appending a new collection",
        default=True,
    )


class AERO_OT_import_level_json(Operator):
    bl_idname = "aero.import_level_json"
    bl_label = "Import Level JSON"
    bl_description = "Read a LevelToJsonMap JSON and build placeholder colliders"
    bl_options = {"REGISTER", "UNDO"}

    filepath: StringProperty(subtype="FILE_PATH")
    filter_glob: StringProperty(default="*.json", options={"HIDDEN"})
    tile_size: FloatProperty(name="Tile size", default=_TILE_DEFAULT, min=0.01, max=10.0)
    thickness: FloatProperty(
        name="Default thickness", default=_THICK_DEFAULT, min=0.05, max=10.0
    )
    replace: BoolProperty(name="Replace collection", default=True)

    def invoke(self, context, event):
        s = context.scene.aero_level_interpreter
        if s.filepath:
            self.filepath = s.filepath
        self.tile_size = s.tile_size
        self.thickness = s.thickness
        self.replace = s.replace
        context.window_manager.fileselect_add(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        if not self.filepath:
            self.report({"ERROR"}, "No JSON path")
            return {"CANCELLED"}
        try:
            name, n, counts = import_level_json(
                self.filepath, self.tile_size, self.thickness, self.replace
            )
        except Exception as e:
            self.report({"ERROR"}, str(e))
            return {"CANCELLED"}
        s = context.scene.aero_level_interpreter
        s.filepath = self.filepath
        s.tile_size = self.tile_size
        s.thickness = self.thickness
        s.replace = self.replace
        summary = ", ".join(f"{k}={v}" for k, v in sorted(counts.items()))
        self.report({"INFO"}, f"{name}: {n} objects ({summary})")
        return {"FINISHED"}


class AERO_OT_import_level_json_panel(Operator):
    bl_idname = "aero.import_level_json_panel"
    bl_label = "Import"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        s = context.scene.aero_level_interpreter
        if not s.filepath:
            self.report({"ERROR"}, "Set a JSON path first")
            return {"CANCELLED"}
        try:
            name, n, counts = import_level_json(
                s.filepath, s.tile_size, s.thickness, s.replace
            )
        except Exception as e:
            self.report({"ERROR"}, str(e))
            return {"CANCELLED"}
        summary = ", ".join(f"{k}={v}" for k, v in sorted(counts.items()))
        self.report({"INFO"}, f"{name}: {n} objects ({summary})")
        return {"FINISHED"}


class AERO_PT_level_interpreter(Panel):
    bl_label = "Level Interpreter"
    bl_idname = "AERO_PT_level_interpreter"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "AeroBoar"

    def draw(self, context):
        s = context.scene.aero_level_interpreter
        layout = self.layout
        layout.prop(s, "filepath")
        row = layout.row(align=True)
        row.prop(s, "tile_size")
        row.prop(s, "thickness")
        layout.prop(s, "replace")
        layout.operator("aero.import_level_json_panel", icon="IMPORT")
        layout.operator("aero.import_level_json", text="Browse JSON…", icon="FILEBROWSER")
        layout.label(text="X along run, Z up, Y thickness (glTF converts)")
        layout.label(text="Re-import replaces Level_<stem>")


def menu_func_import(self, context):
    self.layout.operator(AERO_OT_import_level_json.bl_idname, text="AeroBoar Level JSON (.json)")


classes = (
    LevelInterpreterSettings,
    AERO_OT_import_level_json,
    AERO_OT_import_level_json_panel,
    AERO_PT_level_interpreter,
)


def register():
    for c in classes:
        bpy.utils.register_class(c)
    bpy.types.Scene.aero_level_interpreter = PointerProperty(type=LevelInterpreterSettings)
    bpy.types.TOPBAR_MT_file_import.append(menu_func_import)


def unregister():
    bpy.types.TOPBAR_MT_file_import.remove(menu_func_import)
    del bpy.types.Scene.aero_level_interpreter
    for c in reversed(classes):
        bpy.utils.unregister_class(c)


if __name__ == "__main__":
    register()
