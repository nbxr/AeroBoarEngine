# AeroBoar Blender addons

Install each `.py` from **Edit → Preferences → Add-ons → Install…** (Blender 4.2+).

| Addon | Path | Role |
|-------|------|------|
| ECS Components Editor | `src/ecs_components_editor/ecs_components_editor.py` | Form UI for `ECS_Components_v1` extras (`player`, `locomotion_anim`, …) |
| Animation Transfer | `src/animation_transfer/animation_transfer.py` | Copy clips between matching armatures, scale location to dest size, NLA tracks |

See each folder’s README. Both addons are **scene-agnostic** (no character/scene name checks). Clip names come from extras (`idle` / `walk` / `run`) — `docs/architecture/animation-plan.md` §9.
