bl_info = {
    "name": "ECS Components Editor (AeroBoar)",
    "author": "Nols / AeroBoar",
    "version": (1, 7, 0),
    "blender": (4, 2, 0),
    "location": "Properties > Object > ECS Components",
    "description": "Friendly form editor for the ECS_Components_v1 custom property used by AeroBoar / glTF extras. Supports multi-object edit, Copy from Active, quick actions, multiple scripts, and JSON array/object values.",
    "category": "Object",
    "doc_url": "",
    "tracker_url": "",
}

import bpy
import json
from bpy.props import (
    StringProperty,
    CollectionProperty,
    IntProperty,
    BoolProperty,
    EnumProperty,
    PointerProperty,
)
from bpy.types import (
    PropertyGroup,
    UIList,
    Operator,
    Panel,
)


# ---------------------------------------------------------------------------
# Property Groups
# ---------------------------------------------------------------------------

class ECSComponentPropItem(PropertyGroup):
    """A single key-value pair belonging to a component."""
    key: StringProperty(
        name="Key",
        default="value",
        description="Property name (e.g. mass, max_health, script)",
    )
    value: StringProperty(
        name="Value",
        default="",
        description="Value as text. Numbers, true/false, and JSON arrays/objects ([1,2] or {\"x\":1}) are auto-converted on sync.",
    )


class ECSComponentItem(PropertyGroup):
    """One ECS component (type + optional properties)."""
    type: StringProperty(
        name="Type",
        default="new_component",
        description="Component type name (must be unique on this object)",
    )
    props: CollectionProperty(type=ECSComponentPropItem)
    active_prop_index: IntProperty(default=0)


_PRESET_ITEMS = (
    (
        "tp_human",
        "Third person (human-scale)",
        "player: third_person, boom [0, 1.6, 3] (asset m × worldScale), forward +Z",
    ),
    (
        "tp_small",
        "Third person (small assets)",
        "player: third_person, boom [0, 0.08, 0.15] (asset m × worldScale), forward +Z",
    ),
    (
        "fp",
        "First person (human-scale)",
        "player: first_person, eye_height 1.6",
    ),
    (
        "fp_small",
        "First person (small assets)",
        "player: first_person, eye_height 0.08",
    ),
    (
        "loco_idle",
        "Locomotion (Idle / Walk / Run)",
        "locomotion_anim: Idle / Walk / Run, fade 0.2",
    ),
    (
        "loco_tpose",
        "Locomotion (T-Pose / Walking_A / Running_A)",
        "locomotion_anim clip names used by many Mixamo-style packs, fade 0.2",
    ),
)


class ECSComponentsSettings(PropertyGroup):
    """Per-object storage for the UI list of components."""
    components: CollectionProperty(type=ECSComponentItem)
    active_component_index: IntProperty(default=0)
    auto_sync: BoolProperty(
        name="Auto Sync",
        description="Automatically write to the ECS_Components_v1 custom property when the list changes (applies to all selected objects)",
        default=True,
    )
    preset: EnumProperty(
        name="Preset",
        description="Common ECS component to add or update",
        items=_PRESET_ITEMS,
        default="tp_human",
    )


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _parse_value(text: str):
    """Convert a UI string into bool / int / float / list / dict, otherwise leave as string.

    Arrays and objects are recognized when the text starts with '[' or '{' and
    is valid JSON (e.g. [1.0, 2.0, 3.0] or {"x": 1, "y": 2}).
    """
    text = text.strip()
    if not text:
        return text

    if text.lower() == "true":
        return True
    if text.lower() == "false":
        return False

    # Arrays / objects → native list / dict
    if text[0] in "[{":
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            pass  # fall through and keep as string

    try:
        if "." in text or "e" in text.lower():
            return float(text)
        return int(text)
    except ValueError:
        return text


def serialize_components(settings: ECSComponentsSettings) -> list:
    """Turn the UI collection into the list-of-dicts format.

    Most component types should be unique, but multiple 'script' components
    are explicitly allowed (each with its own name).
    """
    result = []
    seen_types = set()

    for comp in settings.components:
        t = comp.type.strip()
        if not t:
            continue

        # Allow multiple components of type "script"
        if t != "script" and t in seen_types:
            continue
        if t != "script":
            seen_types.add(t)

        entry = {"type": t}
        for p in comp.props:
            key = p.key.strip()
            if key and key != "type":
                entry[key] = _parse_value(p.value)
        result.append(entry)

    return result


def write_to_custom_property(obj, data: list):
    """Write a serialized component list into obj['ECS_Components_v1']."""
    if data:
        obj["ECS_Components_v1"] = data
    elif "ECS_Components_v1" in obj:
        del obj["ECS_Components_v1"]


def write_settings_to_object(obj, settings: ECSComponentsSettings):
    """Serialize the given settings and write them to the object."""
    data = serialize_components(settings)
    write_to_custom_property(obj, data)


def write_settings_to_selected(context, settings: ECSComponentsSettings):
    """Write the given settings to every selected object (and the active one)."""
    data = serialize_components(settings)
    targets = context.selected_objects
    if not targets and context.object:
        targets = [context.object]
    for obj in targets:
        write_to_custom_property(obj, data)


def load_from_custom_property(obj, settings: ECSComponentsSettings):
    """Populate the UI collection from an existing custom property."""
    settings.components.clear()

    raw = obj.get("ECS_Components_v1")
    if raw is None:
        return

    # Support both native list/dict and JSON string
    if isinstance(raw, str):
        try:
            raw = json.loads(raw)
        except json.JSONDecodeError:
            return

    if not isinstance(raw, (list, tuple)):
        return

    for item in raw:
        if not isinstance(item, dict) or "type" not in item:
            continue
        comp = settings.components.add()
        comp.type = str(item["type"])
        for k, v in item.items():
            if k == "type":
                continue
            prop = comp.props.add()
            prop.key = str(k)
            if isinstance(v, bool):
                prop.value = "true" if v else "false"
            elif isinstance(v, (list, dict)):
                prop.value = json.dumps(v)
            else:
                prop.value = str(v)


def _type_aliases(type_name: str) -> set:
    t = type_name.strip()
    if t in ("locomotion_anim", "locomation_anim"):
        return {"locomotion_anim", "locomation_anim"}
    return {t}


def _find_component(settings: ECSComponentsSettings, type_name: str):
    """Return the component item with the given type, or None."""
    aliases = _type_aliases(type_name)
    for comp in settings.components:
        if comp.type.strip() in aliases:
            return comp
    return None


def _ensure_component(settings: ECSComponentsSettings, type_name: str) -> "ECSComponentItem":
    """Find or create a component of the given type and return it."""
    comp = _find_component(settings, type_name)
    if comp is None:
        comp = settings.components.add()
        comp.type = type_name
        settings.active_component_index = len(settings.components) - 1
    return comp


def _set_prop(comp: "ECSComponentItem", key: str, value: str):
    """Set (or add) a key-value property on a component."""
    for p in comp.props:
        if p.key.strip() == key:
            p.value = value
            return
    p = comp.props.add()
    p.key = key
    p.value = value


def _apply_preset(settings: ECSComponentsSettings, key: str) -> str:
    """Ensure the preset's component + props. Returns a short status string."""
    if key in ("tp_human", "tp_small"):
        comp = _ensure_component(settings, "player")
        _set_prop(comp, "camera", "third_person")
        boom = "[0.0, 1.6, 3.0]" if key == "tp_human" else "[0.0, 0.08, 0.15]"
        _set_prop(comp, "boom_offset", boom)
        _set_prop(comp, "forward", "[0, 0, 1]")
        return "player third_person"
    if key in ("fp", "fp_small"):
        comp = _ensure_component(settings, "player")
        _set_prop(comp, "camera", "first_person")
        _set_prop(comp, "eye_height", "0.08" if key == "fp_small" else "1.6")
        return "player first_person"
    if key in ("loco_idle", "loco_tpose"):
        comp = _ensure_component(settings, "locomotion_anim")
        comp.type = "locomotion_anim"
        if key == "loco_tpose":
            _set_prop(comp, "idle", "T-Pose")
            _set_prop(comp, "walk", "Walking_A")
            _set_prop(comp, "run", "Running_A")
        else:
            _set_prop(comp, "idle", "Idle")
            _set_prop(comp, "walk", "Walk")
            _set_prop(comp, "run", "Run")
        _set_prop(comp, "fade", "0.2")
        return "locomotion_anim"
    return key


# ---------------------------------------------------------------------------
# UI Lists
# ---------------------------------------------------------------------------

class ECS_UL_components(UIList):
    bl_idname = "ECS_UL_components"

    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index):
        if self.layout_type in {'DEFAULT', 'COMPACT'}:
            layout.prop(item, "type", text="", emboss=False, icon='NODE')
        elif self.layout_type == 'GRID':
            layout.alignment = 'CENTER'
            layout.label(text=item.type, icon='NODE')


class ECS_UL_component_props(UIList):
    bl_idname = "ECS_UL_component_props"

    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index):
        if self.layout_type in {'DEFAULT', 'COMPACT'}:
            row = layout.row(align=True)
            row.prop(item, "key", text="", emboss=False)
            row.prop(item, "value", text="", emboss=False)
        elif self.layout_type == 'GRID':
            layout.alignment = 'CENTER'
            layout.label(text=item.key)


# ---------------------------------------------------------------------------
# Operators
# ---------------------------------------------------------------------------

class ECS_OT_add_component(Operator):
    bl_idname = "ecs.add_component"
    bl_label = "Add Component"
    bl_description = "Add a new ECS component to the active object (and sync to all selected if Auto Sync is on)"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        obj = context.object
        if not obj:
            return {'CANCELLED'}
        settings = obj.ecs_components_settings
        item = settings.components.add()
        item.type = "new_component"
        settings.active_component_index = len(settings.components) - 1
        if settings.auto_sync:
            write_settings_to_selected(context, settings)
        return {'FINISHED'}


class ECS_OT_remove_component(Operator):
    bl_idname = "ecs.remove_component"
    bl_label = "Remove Component"
    bl_description = "Remove the selected ECS component (and sync to all selected if Auto Sync is on)"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        obj = context.object
        if not obj:
            return {'CANCELLED'}
        settings = obj.ecs_components_settings
        idx = settings.active_component_index
        if 0 <= idx < len(settings.components):
            settings.components.remove(idx)
            settings.active_component_index = min(idx, len(settings.components) - 1)
            if settings.auto_sync:
                write_settings_to_selected(context, settings)
        return {'FINISHED'}


class ECS_OT_add_prop(Operator):
    bl_idname = "ecs.add_prop"
    bl_label = "Add Property"
    bl_description = "Add a key-value property to the selected component (and sync to all selected if Auto Sync is on)"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        obj = context.object
        if not obj:
            return {'CANCELLED'}
        settings = obj.ecs_components_settings
        idx = settings.active_component_index
        if not (0 <= idx < len(settings.components)):
            return {'CANCELLED'}
        comp = settings.components[idx]
        prop = comp.props.add()
        prop.key = "property"
        prop.value = ""
        comp.active_prop_index = len(comp.props) - 1
        if settings.auto_sync:
            write_settings_to_selected(context, settings)
        return {'FINISHED'}


class ECS_OT_remove_prop(Operator):
    bl_idname = "ecs.remove_prop"
    bl_label = "Remove Property"
    bl_description = "Remove the selected property from the component (and sync to all selected if Auto Sync is on)"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        obj = context.object
        if not obj:
            return {'CANCELLED'}
        settings = obj.ecs_components_settings
        idx = settings.active_component_index
        if not (0 <= idx < len(settings.components)):
            return {'CANCELLED'}
        comp = settings.components[idx]
        pidx = comp.active_prop_index
        if 0 <= pidx < len(comp.props):
            comp.props.remove(pidx)
            comp.active_prop_index = min(pidx, len(comp.props) - 1)
            if settings.auto_sync:
                write_settings_to_selected(context, settings)
        return {'FINISHED'}


class ECS_OT_sync_to_property(Operator):
    bl_idname = "ecs.sync_to_property"
    bl_label = "Sync to Custom Property"
    bl_description = "Write the current component list into the ECS_Components_v1 custom property on ALL selected objects"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        obj = context.object
        if not obj:
            return {'CANCELLED'}
        settings = obj.ecs_components_settings
        write_settings_to_selected(context, settings)
        count = len(context.selected_objects) or 1
        self.report({'INFO'}, f"Synced ECS_Components_v1 to {count} object(s)")
        return {'FINISHED'}


class ECS_OT_load_from_property(Operator):
    bl_idname = "ecs.load_from_property"
    bl_label = "Load from Custom Property"
    bl_description = "Read the existing ECS_Components_v1 custom property from the active object into the editor"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        obj = context.object
        if not obj:
            return {'CANCELLED'}
        load_from_custom_property(obj, obj.ecs_components_settings)
        self.report({'INFO'}, "Loaded ECS_Components_v1 from active object")
        return {'FINISHED'}


class ECS_OT_clear_all(Operator):
    bl_idname = "ecs.clear_all"
    bl_label = "Clear All"
    bl_description = "Remove all components from the active object and clear the property on all selected objects"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        obj = context.object
        if not obj:
            return {'CANCELLED'}
        settings = obj.ecs_components_settings
        settings.components.clear()
        settings.active_component_index = 0

        # Clear on every selected object
        targets = context.selected_objects
        if not targets:
            targets = [obj]
        for o in targets:
            if "ECS_Components_v1" in o:
                del o["ECS_Components_v1"]
        return {'FINISHED'}


class ECS_OT_copy_from_active(Operator):
    bl_idname = "ecs.copy_from_active"
    bl_label = "Copy from Active"
    bl_description = "Copy ECS Components from the active object and replace them on all other selected objects"
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, context):
        return (
            context.object is not None
            and len(context.selected_objects) > 1
        )

    def execute(self, context):
        active = context.object
        if not active:
            return {'CANCELLED'}

        # Prefer the live UI list of the active object; fall back to its custom property
        settings = active.ecs_components_settings
        data = serialize_components(settings)

        # If the UI list is empty, try loading from the custom property instead
        if not data:
            raw = active.get("ECS_Components_v1")
            if isinstance(raw, str):
                try:
                    raw = json.loads(raw)
                except json.JSONDecodeError:
                    raw = None
            if isinstance(raw, (list, tuple)):
                data = [item for item in raw if isinstance(item, dict) and "type" in item]

        count = 0
        for obj in context.selected_objects:
            if obj == active:
                continue
            write_to_custom_property(obj, data)
            # Also refresh the UI settings on the target so the panel stays consistent
            # if the user later activates it
            load_from_custom_property(obj, obj.ecs_components_settings)
            count += 1

        self.report({'INFO'}, f"Copied ECS Components to {count} object(s)")
        return {'FINISHED'}


class ECS_OT_set_as_player(Operator):
    bl_idname = "ecs.set_as_player"
    bl_label = "Set as Player"
    bl_description = "Add a 'player' tag component (unique). Syncs to all selected objects if Auto Sync is on"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        obj = context.object
        if not obj:
            return {'CANCELLED'}
        settings = obj.ecs_components_settings
        _ensure_component(settings, "player")
        if settings.auto_sync:
            write_settings_to_selected(context, settings)
        else:
            write_settings_to_object(obj, settings)
        self.report({'INFO'}, "Set as player")
        return {'FINISHED'}


class ECS_OT_add_preset(Operator):
    bl_idname = "ecs.add_preset"
    bl_label = "Add Preset"
    bl_description = (
        "Add or update a common component from the Preset dropdown "
        "(player camera or locomotion_anim)"
    )
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        obj = context.object
        if not obj:
            return {'CANCELLED'}
        settings = obj.ecs_components_settings
        label = _apply_preset(settings, settings.preset)
        if settings.auto_sync:
            write_settings_to_selected(context, settings)
        else:
            write_settings_to_object(obj, settings)
        self.report({'INFO'}, f"Added preset: {label}")
        return {'FINISHED'}


class ECS_OT_add_script(Operator):
    bl_idname = "ecs.add_script"
    bl_label = "Add Script"
    bl_description = "Add a new 'script' component. Multiple scripts are allowed (each with its own name)"
    bl_options = {'REGISTER', 'UNDO'}

    script_name: StringProperty(
        name="Script Name",
        description="Name of the script (e.g. VrGrab, Explodes, player_controller)",
        default="new_script",
    )

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self)

    def draw(self, context):
        self.layout.prop(self, "script_name")

    def execute(self, context):
        obj = context.object
        if not obj:
            return {'CANCELLED'}
        settings = obj.ecs_components_settings
        name = self.script_name.strip() or "new_script"

        # Always add a new script component (multiple scripts are allowed)
        comp = settings.components.add()
        comp.type = "script"
        _set_prop(comp, "name", name)
        settings.active_component_index = len(settings.components) - 1

        if settings.auto_sync:
            write_settings_to_selected(context, settings)
        else:
            write_settings_to_object(obj, settings)
        self.report({'INFO'}, f"Added script: {name}")
        return {'FINISHED'}


class ECS_OT_add_trigger(Operator):
    bl_idname = "ecs.add_trigger"
    bl_label = "Add Trigger"
    bl_description = "Add/ensure a collider component with isTrigger=true (default shape: box). Syncs to selected if Auto Sync is on"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        obj = context.object
        if not obj:
            return {'CANCELLED'}
        settings = obj.ecs_components_settings
        comp = _ensure_component(settings, "collider")
        _set_prop(comp, "isTrigger", "true")
        # Only set a default shape if none exists yet
        has_shape = any(p.key.strip() == "shape" for p in comp.props)
        if not has_shape:
            _set_prop(comp, "shape", "box")
        if settings.auto_sync:
            write_settings_to_selected(context, settings)
        else:
            write_settings_to_object(obj, settings)
        self.report({'INFO'}, "Added trigger collider")
        return {'FINISHED'}


# ---------------------------------------------------------------------------
# Panel
# ---------------------------------------------------------------------------

class ECS_PT_components_panel(Panel):
    bl_label = "ECS Components"
    bl_idname = "ECS_PT_components_panel"
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "object"
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        return context.object is not None

    def draw_header(self, context):
        self.layout.label(text="", icon='NODETREE')

    def draw(self, context):
        layout = self.layout
        obj = context.object
        settings = obj.ecs_components_settings

        # Top toolbar
        row = layout.row(align=True)
        row.operator("ecs.load_from_property", icon='IMPORT', text="Load")
        row.operator("ecs.sync_to_property", icon='EXPORT', text="Sync")
        row.prop(settings, "auto_sync", text="", icon='FILE_REFRESH')

        # Multi-object tools
        row = layout.row(align=True)
        row.operator("ecs.copy_from_active", icon='COPYDOWN', text="Copy from Active")

        sel_count = len(context.selected_objects)
        if sel_count > 1:
            layout.label(text=f"{sel_count} objects selected – Sync / Auto Sync write to all", icon='INFO')

        # Quick actions
        layout.separator()
        box = layout.box()
        box.label(text="Quick Actions", icon='MODIFIER')
        row = box.row(align=True)
        row.prop(settings, "preset", text="")
        row.operator("ecs.add_preset", icon='ADD', text="Add")
        row = box.row(align=True)
        row.operator("ecs.set_as_player", icon='USER', text="Set as Player")
        row.operator("ecs.add_script", icon='TEXT', text="Add Script")
        row.operator("ecs.add_trigger", icon='FORCE_FORCE', text="Add Trigger")

        layout.separator()

        # Component list
        row = layout.row()
        row.template_list(
            "ECS_UL_components",
            "components",
            settings,
            "components",
            settings,
            "active_component_index",
            rows=4,
        )

        col = row.column(align=True)
        col.operator("ecs.add_component", icon='ADD', text="")
        col.operator("ecs.remove_component", icon='REMOVE', text="")
        col.separator()
        col.operator("ecs.clear_all", icon='TRASH', text="")

        # Selected component details
        idx = settings.active_component_index
        if 0 <= idx < len(settings.components):
            comp = settings.components[idx]

            layout.separator()
            box = layout.box()
            box.label(text=f"Component: {comp.type}", icon='NODE')
            box.prop(comp, "type", text="Type")
            t = (comp.type or "").strip().lower()
            if t == "player":
                box.label(
                    text="Keys: camera, boom_offset, forward, yaw_offset, "
                    "eye_offset"
                )
            elif t in ("locomotion_anim", "locomation_anim"):
                box.label(text="Keys: idle, walk, run, fade, walk_speed, run_speed")

            # Properties of this component
            row = box.row()
            row.template_list(
                "ECS_UL_component_props",
                "props",
                comp,
                "props",
                comp,
                "active_prop_index",
                rows=3,
            )
            col = row.column(align=True)
            col.operator("ecs.add_prop", icon='ADD', text="")
            col.operator("ecs.remove_prop", icon='REMOVE', text="")

        # Status / preview
        layout.separator()
        data = serialize_components(settings)
        if data:
            box = layout.box()
            box.label(text="Preview (will be written to extras):", icon='INFO')
            preview = json.dumps(data, indent=2)
            # Show a short preview
            for line in preview.splitlines()[:12]:
                box.label(text=line)
            if len(preview.splitlines()) > 12:
                box.label(text="…")
        else:
            layout.label(text="No components defined", icon='INFO')


# ---------------------------------------------------------------------------
# Registration
# ---------------------------------------------------------------------------

classes = (
    ECSComponentPropItem,
    ECSComponentItem,
    ECSComponentsSettings,
    ECS_UL_components,
    ECS_UL_component_props,
    ECS_OT_add_component,
    ECS_OT_remove_component,
    ECS_OT_add_prop,
    ECS_OT_remove_prop,
    ECS_OT_sync_to_property,
    ECS_OT_load_from_property,
    ECS_OT_clear_all,
    ECS_OT_copy_from_active,
    ECS_OT_set_as_player,
    ECS_OT_add_preset,
    ECS_OT_add_script,
    ECS_OT_add_trigger,
    ECS_PT_components_panel,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    bpy.types.Object.ecs_components_settings = PointerProperty(type=ECSComponentsSettings)


def unregister():
    del bpy.types.Object.ecs_components_settings
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)


if __name__ == "__main__":
    register()
