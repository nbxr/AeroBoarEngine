# Animation Transfer (AeroBoar)

Copy armature clips from a **source** character onto a **destination** with the same bone names. Optionally scale location keys to the destination rest size, stash NLA tracks for glTF export, and delete the source.

Works on any scene: pick two armatures (or skinned meshes / character empties). Bone names are matched exactly, then case-insensitively (`hips` ↔ `Hips`).

## Installation

1. Blender → **Edit → Preferences → Add-ons**
2. **Install…** and select `animation_transfer.py`
3. Enable **Animation Transfer (AeroBoar)**

Blender **4.2+** (4.4 / 5.x layered Actions: F-curves live on channelbags).

## Typical run

1. Select **source**, then **destination** (destination **active**).
2. **N** panel → **AeroBoar → Animation Transfer** (or Object Properties).
3. **Detect source → dest**, or pick the pointers by hand. Confirm bone match count.
4. Copy method **Duplicate Actions** (this only picks *how* — it does not run).
5. Leave **Scale location to dest size** on if the characters differ in size.
6. Click **Transfer Animations (deletes source)** (or uncheck Delete source first).
7. Scrub a locomotion clip: standing feet near the armature origin.
8. Export dest: **glTF 2.0 → Animation → NLA Tracks**, **Include → Custom Properties**.

**Duplicate Actions** is the copy method. **Transfer Animations** is the run button.

## Location scale

Rotations copy as-is. **Location** keys (root/hips bounce) are in the *source* rest size. If dest is smaller, a 1:1 copy makes locomotion look like the torso leaves the legs.

Auto factor = dest rest hip height / source rest hip height (world Z above the armature object). If auto is `1.0` but dest is clearly smaller (Apply Scale already baked), type a factor.

**Scale dest clip locations** — apply that factor to clips already on dest. Click **once**. Leave **Skip idle/bind clips** on if Idle / T-Pose / bind / rest are already dest-sized.

This is **not** IK. IK (engine, later) keeps authored hip bounce and plants feet on the ground.

### Modes

| Mode | When |
|------|------|
| **Duplicate Actions** | Same bone names. Copies F-curves, then scales location if enabled. |
| **Move + Constrain + Bake** | Rest poses differ. Snaps source onto dest, Copy Rotation, bakes visual keys. Optional root/hips location. |

## Engine extras

Clip names are whatever you put on `locomotion_anim` (`idle` / `walk` / `run`). Common packs: `Idle`/`Walk`/`Run` or `T-Pose`/`Walking_A`/`Running_A`. See `docs/architecture/animation-plan.md` §9.

## Safety

- Refuses to delete if source and destination share a hierarchy.
- Does not move the destination. Bake mode only temporarily snaps the **source** onto dest for constraints.

## See also

- `tools/blender/src/ecs_components_editor/` — player / locomotion extras UI
- `docs/architecture/animation-plan.md` §9
