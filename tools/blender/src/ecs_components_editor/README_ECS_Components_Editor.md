# ECS Components Editor (AeroBoar)

A Blender addon that gives you a clean form/UI to edit the `ECS_Components_v1` custom property used by AeroBoar (and compatible with the tiny glTF ECS Components specification).

Supports multi-object editing, “Copy from Active”, quick-action buttons, and multiple `script` components.

## Installation

1. Open Blender → **Edit → Preferences → Add-ons**
2. Click **Install...** and select `ecs_components_editor.py`
3. Enable the addon **"ECS Components Editor (AeroBoar)"**

(Alternatively just drop the `.py` into your Blender `scripts/addons` folder and enable it.)

## Usage

1. Select any object (or multiple objects).
2. Go to the **Object Properties** tab (the orange cube icon).
3. Scroll down to the **ECS Components** panel (it starts collapsed).

### Main controls

- **Load** – Reads the `ECS_Components_v1` custom property from the **active** object into the editor.
- **Sync** – Writes the current list into the `ECS_Components_v1` custom property on **all selected objects**.
- The small refresh icon toggles **Auto Sync**. When enabled, every edit (add/remove component or property) is immediately written to all selected objects.
- **Copy from Active** – Copies the ECS Components from the active object and **replaces** them on every other selected object. Enabled only when more than one object is selected.

When multiple objects are selected a small info line appears reminding you that Sync / Auto Sync affect the whole selection.

### Quick Actions

**Preset dropdown + Add** writes a common component (updates it if it already exists):

| Preset | Writes |
|--------|--------|
| **Third person (human-scale)** | `player`: `third_person`, `boom_offset` `[0, 1.6, 3]`, `forward` `[0, 0, 1]` |
| **Third person (small assets)** | same, boom `[0, 0.08, 0.15]` |
| **First person (human-scale)** | `player`: `first_person`, `eye_height` `1.6` |
| **First person (small assets)** | `player`: `first_person`, `eye_height` `0.08` |
| **Locomotion (Idle / Walk / Run)** | `locomotion_anim` clip names `Idle` / `Walk` / `Run`, fade 0.2 |
| **Locomotion (T-Pose / Walking_A / Running_A)** | same type, Mixamo-style clip names, fade 0.2 |

Boom and eye height are **asset meters × `worldScale`**. `forward` `[0, 0, 1]` is Blender/glTF +Z vs engine camera −Z. Edit clip names after Add if the pack differs.

| Button          | What it does                                                                 |
|-----------------|------------------------------------------------------------------------------|
| **Set as Player** | Adds a bare `player` tag (no camera keys).                                 |
| **Add Script**    | Opens a dialog for the script name, then **adds a new** `script` component. Multiple scripts are allowed. |
| **Add Trigger**   | Ensures a `collider` component with `isTrigger: true` (default shape `box` if none set). |

Presets and buttons respect Auto Sync (they write to every selected object when it is enabled).

### Editing

- Use the **+ / −** buttons next to the component list to add/remove components.
- Click a component to edit its **Type** and its key-value properties.
- Values are entered as text. On sync the addon automatically converts:
  - `"true"` / `"false"` → boolean
  - integers and floats → numbers
  - JSON arrays / objects → native list / dict  
    e.g. `[1.0, 2.0, 3.0]` or `{"x": 1, "y": 2}`
  - everything else stays a string

### Typical multi-object workflow

1. Configure one object fully (e.g. a prop with `player` or a collider).
2. Select the others of the same kind + the configured one (configured object active).
3. Click **Copy from Active**.
4. Adjust mass or other per-piece values individually if needed, then hit **Sync**.

### Exporting to glTF

When you export:

1. File → Export → glTF 2.0
2. Make sure **Include → Custom Properties** is enabled
3. The `ECS_Components_v1` list will appear in the node’s `extras` exactly as the specification expects.

Example of what ends up in the glTF (human-scale third person):

```json
"extras": {
  "ECS_Components_v1": [
    { "type": "rigidbody", "bodyType": "dynamic", "mass": 1.0 },
    { "type": "collider", "shape": "convex", "isTrigger": false },
    { "type": "script", "name": "VrGrab" },
    { "type": "player", "camera": "third_person", "boom_offset": [0, 1.6, 3],
      "forward": [0, 0, 1] },
    { "type": "locomotion_anim", "idle": "Idle", "walk": "Walk", "run": "Run",
      "fade": 0.2 }
  ]
}
```

Use the **small assets** presets when the mesh is centimetre-scale (`boom_offset` `[0, 0.08, 0.15]`, `eye_height` `0.08`). Player extras the engine reads: `camera` / `boom_offset` (asset meters × `worldScale`), `forward` / `yaw_offset`, `eye_offset` / `eye_height`.

## Notes

- Most component types should be unique, but **multiple `script` components are allowed** (each with its own `name`).
- The addon stores a native Python list/dict structure in the custom property, which the official Khronos glTF exporter handles correctly.
- Works with Blender 4.2+ (tested against the 4.x / 5.x series).
- Version 1.4 parses JSON arrays/objects into native lists/dicts (no longer left as quoted strings).

Enjoy building AeroBoar entities!
