# Level Interpreter (AeroBoar)

Blender 4.2+ addon. Reads **LevelToJsonMap** JSON (`width`, `height`, `level_objects[]`) and builds a side-scroller collection of placeholder meshes plus **KHR box colliders**.

Example: `W:\git\LevelToJsonMap\LevelToJsonMap\Maps\smb1-1.json` (SMB 1-1).

## Layout

| JSON | Blender | After glTF export (engine) |
|------|---------|----------------------------|
| `x` along the run | **X** | **X** |
| `y` up from the bottom of the map | **Z** (Blender up) | **Y** |
| slab depth (`thickness`, default **1.0** tiles) | **Y** | **Z** |
| 1 tile | **1.0 m** (before engine `worldScale`) | same |

Author in Blender **Z-up**. The Khronos glTF exporter converts to engine/glTF **Y-up**.

`x,y` is the **bottom-left** of each `width×height` rect. Object origin is the box center.

Optional per-object **`thickness`** (tiles, same units as `width`/`height`) is the slab depth along Blender **Y**. If omitted, it defaults to **1.0** (or the panel **Default thickness**). Typical values: **2.0** for `ground` / `pipe` / `pipe_end`, **1.0** for blocks (`bricks`, `question`, `stairs`, `castle`) and flagpoles.

## Placeholders (until real assets)

| `type` | Mesh | Color |
|--------|------|--------|
| `ground` | box | brown |
| `bricks` | box | orange |
| `question` | box | yellow (tint by `contents`) |
| `pipe` | cylinder on Z (Blender up) | green |
| `pipe_end` | wider cylinder | brighter green |
| `stairs` | box | gray |
| `castle` | box | stone |
| `flag_pole` | thin cylinder | light gray |
| `flag_end` | sphere | red |

Unknown types become gray boxes. `contents` (`coins` / `mushroom` / `star`) is stored on the object and tints question/brick materials.

## Physics

Each mesh gets a custom property `KHR_physics_rigid_bodies` (JSON string):

```json
{ "collider": { "geometry": { "box": { "size": [1, 1, 1] } } } }
```

No `motion` → **static**. Local box is unit-sized; **object scale is the full extents**. Engine `spawn_scene_physics` reads this from node **extras** (and extensions) and applies node scale.

Also written: `level_type`, `level_contents`, `level_tile` `[x,y,w,h]`, `level_thickness`.

Export glTF with **Custom Properties** enabled so extras survive. Later: swap placeholder meshes for real assets; keep names / custom props / scale.

## Install

1. Blender → **Edit → Preferences → Add-ons → Install…**
2. Select `level_interpreter.py`
3. Enable **Level Interpreter (AeroBoar)**

## Usage

- **File → Import → AeroBoar Level JSON**
- Or **3D View → Sidebar → AeroBoar → Level Interpreter** (path, tile size, default thickness)

**Replace collection** (default): rebuilds `Level_<filename-stem>` (e.g. `Level_smb1-1`) and deletes the previous import. Off → append `Level_smb1-1_001`.

Root empty `Level_*_root` parents every tile.

## Out of scope (this version)

Enemies / items as spawned actors (not in smb1-1.json). Dynamic question-block motion. Real mesh library. CSM / gameplay scripts.
