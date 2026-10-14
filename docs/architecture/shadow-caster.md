# Tight Shadow-Caster Culling for Cascaded Shadow Maps

Cascaded shadow maps split the view frustum into distance slices (cascades). Each cascade has its own orthographic shadow map. The usual first culling step keeps every object that intersects the cascade’s light-space frustum. That volume is far larger than necessary: many objects can never darken any surface the camera actually sees.

A tighter test asks the opposite question: *can this object cast a shadow onto any receiver that belongs to this cascade?* The answer is obtained by constructing a convex *caster volume* from the cascade planes and then testing object bounds against that volume.

## 1. Receiver volume

The receivers for a given cascade are all surfaces that:

- lie inside the corresponding view-frustum slice, and
- will be shaded with that cascade’s shadow map.

In practice the slice is represented by the six planes of a truncated pyramid (the “cut pyramid”). Those six planes already enclose every possible receiver; any tighter bound (convex hull of the actual visible geometry) is optional and more expensive.

## 2. Front / back classification

Let \(\mathbf{L}\) be the unit light direction (the direction shadow rays travel). For each cascade plane with outward normal \(\mathbf{N}\) compute

\[
s = \mathbf{N} \cdot \mathbf{L}.
\]

- \(s < 0\): the plane faces the light → **front plane** (discard it).
- \(s > 0\): the plane faces away from the light → **back plane** (keep it, flip \(\mathbf{N}\) so it points inward).

The discarded front planes are the reason objects that sit outside the camera frustum can still survive: the volume is allowed to extend toward the light.

## 3. Silhouette edges

An edge of the cascade polyhedron is a silhouette edge precisely when it is shared by one remaining front plane and one back plane. Those edges are the intersection lines of every such pair:

\[
\mathbf{D} = \mathbf{N}_\text{front} \times \mathbf{N}_\text{back}.
\]

A point on the line can be found by solving the two plane equations together with a third auxiliary plane whose normal is \(\mathbf{D}\). Because the later clip plane only needs to contain the whole infinite line, the finite segment is unnecessary.

A typical six-plane frustum yields four (sometimes five) silhouette lines.

## 4. Extra clip planes

Each silhouette line together with the light direction defines one new plane. The two tangent vectors are \(\mathbf{D}\) and \(\mathbf{L}\); their cross product is the plane normal:

\[
\mathbf{N}_\text{clip} = \mathbf{D} \times \mathbf{L}.
\]

Orient \(\mathbf{N}_\text{clip}\) so that it points inward (test a known interior point of the cascade). The resulting plane contains the silhouette edge and is parallel to the light rays.

## 5. The finished caster volume

The convex polyhedron is the intersection of

- the kept (flipped) back planes of the cascade, and
- the newly generated silhouette planes.

Toward the light the volume is unbounded (or bounded only by an engine-wide max-shadow-distance plane). Behind the receivers the original back planes already close it. Laterally the silhouette planes cut away any region whose projection misses the receivers.

This polyhedron is the exact set of points that can cast a shadow onto the chosen receiver polyhedron, assuming infinite precision and a directional light.

## 6. Testing whether an object can cast a visible shadow

An object is a potential caster for the cascade if and only if its bounding volume intersects the caster polyhedron.

### Plane test (most common)

Represent the object by a conservative bound—AABB, bounding sphere, or OBB. For every plane of the caster volume compute the signed distance of the bound. If the bound lies completely in the positive half-space of any plane, the object is outside and can be discarded. If it intersects every half-space it is kept and must be rendered into that cascade’s shadow map.

Because the test uses a bound rather than the true mesh, two kinds of error appear:

- **False positive** (object kept but its geometry cannot reach any receiver) – only costs extra draw calls.
- **False negative** (object culled while part of it can still reach a receiver) – produces a disappearing shadow. This happens when the bound is smaller than the real geometry or when the receiver polyhedron itself does not fully contain every visible surface.

### After the object is drawn into the shadow map

A fragment on a visible receiver is actually in shadow when:

1. The receiver belongs to this cascade (its world position falls inside the cascade’s split interval and inside the cascade’s XY bounds in light space).
2. The reconstructed light-space depth of the receiver is greater than the value stored in the shadow map (plus bias). That stored value came from some caster that passed the volume test.

Thus the geometric volume test only answers “might this object darken something the camera sees?” The shadow-map depth comparison answers “does it actually do so for this particular receiver fragment?”

## 7. Objects outside the camera frustum

Because the original front planes were discarded, the caster volume extends outside the view frustum both sideways and toward the light. An object that the camera cannot see can still intersect the volume and is therefore retained. Its shadow appears on screen exactly when a visible receiver samples the corresponding shadow-map texel and fails the depth test.

## 8. Sizing and the “margin” question

The silhouette construction already computes the exact caster volume of the polyhedron you supplied. A world-space margin on the planes is only a cheap way to absorb two approximations that exist in every real engine:

- the cascade frustum is larger (or occasionally slightly smaller) than the true set of receivers,
- object bounds are imperfect.

You can eliminate the margin by feeding a tighter receiver hull (convex hull of the actual visible triangles in the cascade) and tighter caster hulls, but the extra CPU cost is rarely justified. The analytic size is therefore known; the practical size includes a few centimetres of safety because the inputs are themselves approximate.

## 9. Typical reasons shadows vanish too early

- Sign error in the light-direction dot product → a plane is dropped or a clip plane is flipped.
- A front/back pair was forgotten, so one silhouette plane is missing.
- Object bounds used for the plane test do not contain the vertices that actually cast.
- Cascade split distances or fitting leave a sliver of visible geometry outside the planes that defined the receiver volume.

Drawing the final set of planes in a debug view immediately shows whether a disappearing caster still intersects the volume. If it does not, the missing or reversed plane is the defect; a margin only hides the defect.

## 10. Engine notes

Implemented in `gfx::build_caster_volume` (`ShadowCasterVolume.cpp`). Wiring: `lighting-implementation.md` §4.1.

- Frustum planes are **inward** (`dot(n,p)+d >= 0`). Doc §2 uses outward **N**; front ⇔ `N_inward · L_shadow > 0` with `L_shadow = -to_light` (KHR directional `to_light` is NdotL).
- Silhouette edges are the **12 truncated-pyramid edges**, not every front/back pair.
- Grazing (`N · L ≈ 0`): **keep**. The plane is already parallel to the light; it is a lateral bound, not a front to drop. Do not special-case near/far.
- GPU test: `cull_frustum.comp` `extra_planes[16]`. `skip_frustum_cull` ignores the 6 shadow-ortho planes; extra count 0 emits every opaque (degenerate).
- Map fit is the light-space AABB of the cut pyramid (Z extruded toward the light by scene extent). Texel snap in 0.25 mm steps on a light basis that does not follow the camera. Vulkan Y flip negates **both** the ortho scale and the Y translation (`proj[1][1]` and `proj[3][1]`). Scale alone is wrong unless the window is centered on zero.
- `GpuCullGlobals` is one UBO per FIF/pass, written with **`vkCmdUpdateBuffer`** (GpuOnly + `TRANSFER_DST`). Do not host-`memcpy` it: several `record()` calls share the buffer in one CB (see §11.3).

## 11. Shadow troubleshooting log

Pickup file for CSM / caster-volume work. Algorithm is §1–10. **This section is the status, harness, closed bugs, and next steps.** Date of last update: 2026-09-29.

**Policy (do not regress):** implement the volume faithfully. If it still fails, that is a research problem — do not “fix” it with pads. Rejected: % pad on planes, nested `0..s0` ranges, small-scene one-map, union-all-casters, skip-frustum as emit-all, treating grazing near/far as front, post-hoc plane filters, depth clamp as a volume substitute.

### 11.1 Status

| | State |
|--|--------|
| **Works** | Caster-volume construction (§2–5). Reverse-Z CSM, PCF, texel-snap ortho, GPU extra-plane cull. Off-screen Queen_B A/B (§11.3). On-screen pawn receivers vs cut pyramid (§11.4). Receiver acne offset in **meters per texel** (§11.5), so pawn shadow strength does not track cascade depth range. Cascade draws wait for vertex and draw-indirect before the next cull overwrites the instance buffer (§11.6). The shaded cascade is the view-depth slice, and those slices are equal thirds of the scene in front of the camera (§11.7). |
| **Broken** | Nothing measured. Re-open from a locked pose where self-shadow or board contact still vanishes after §11.5. |
| **Not started** | TCF, CascadeBake / far-cascade cache, spot atlas. |
| **As left in the repo** | `cameraOverride.enabled=false` (fly). Poses **A**, **B** (Queen), **P** (boom census), **G** (low far view). `shadows.depthBias=false`. Window visible. The shadow probe and offline probe programs are gone. |

### 11.2 Harness (run this, do not invent a parallel path)

**Scene:** `ABeautifulGameGame`, `worldScale` 10, `shadows.silhouette` true, `shadows.depthBias` **false** (raster slope bias is off; see §11.5). CWD **must** be `build/` (`VS_DEBUGGER_WORKING_DIRECTORY`). Binary: `build/Debug/Aero_Boar_Engine.exe`. Log: `build/aero_boar.log`. `copy_configuration` copies `assets/scenes/configuration.json` → `build/assets/scenes/` on build; if you edit JSON without rebuilding, copy it yourself. **Do not** rewrite the file with PowerShell `ConvertTo-Json` (it mangles numbers / UTF-8).

**Freeze the eye**

- `cameraOverride`: `{ "enabled": true, "pose": "A"|"B"|…, "A": { "position", "forward" }, "B": { … } }`. Also accepts a flat `{ enabled, position, forward }` (what **P** prints).
- When enabled, `camera.pose_locked=true`. **FpsMove / boom / DesktopMove must not write the eye** (that was a real bug: override matched the log, then locomotion overwrote it).
- **P**: logs `[Camera] pos=… forward=…` and a pasteable `cameraOverride` object. Capture a failing view with P, store it as a named pose, do not eyeball.

**`debug` keys** (`Configuration::DebugOptions`)

| Key | Role |
|-----|------|
| `exitAfterFrames` / `hiddenWindow` | Headless A/B (`12` / `true` is enough). Restore `0` / `false` after. |
| `clearLogOnStart` | Truncate `aero_boar.log` on boot. |
| `verbose` / `logCull` | Load chatter / `[Cull]` lines. Keep off unless needed. Errors and P/F3/F4 stay `LOG_INFO`. |

**Build / A/B loop**

```text
cmake --build --preset windows-debug --target Aero_Boar_Engine
# CWD = build/
.\Debug\Aero_Boar_Engine.exe
```

The in-engine `[ShadowProbe]` logger, shadow-map readback, and the offline programs `tools/shadow_volume_probe.cpp` / `tools/queen_ab_probe.cpp` were removed once the measured failures were closed. The tables below are that record. Do not reintroduce a probe to "check" a hunch; a new failure needs a new measurement, not the old harness.

Flip `"pose": "A"` ↔ `"B"` in **both** source JSON and `build/assets/scenes/configuration.json` if you skip the copy target.

**Historical `[ShadowProbe]` lines** (the logger that printed these is gone)

- `R.board` = head projected along `L_shadow = -to_light` onto AABB min.y (board). `shade_i` = cascade `pbr.frag` would sample (`dot(p-cam, forward)` vs splits, 18% blend).
- Per cascade: `pyr` = R in cut pyramid; `vol` = C AABB vs `last_caster`; `mapC`/`mapR` = AABB/point in shadow NDC; `head_uvz` from `last_view_proj * head` (ZCLIP if z∉[0,1]).
- Tags: `P1 sampling!=pyramid` (shade index ≠ pyramid), `P2 volume excludes C`, `P3 ortho miss`.
- Reverse-Z depth: **0 = empty/clear**, board ~0.13, feet ~0.16, body ~0.20, Queen crown ~0.31. In shadow if stored **≥** board_z.
- CPU `extra_emit` / `extra_worst`: render-mesh AABB vs caster planes, same pad as the shader (1% extent, fail if supporting vertex `< -0.02`).
- CPU `ortho6_pass`: same AABB vs 6 planes of `last_view_proj` (the test `skip_frustum_cull` skips on GPU).
- GPU count: `copy_opaque_instances` **after cas1 dispatch**, then `count_copied_models_near` after that FIF’s fence. Trigger is `g_inst_slot == -1` (the wait path sets **`-2`**, not a retrigger). The scan is the **whole** opaque instance buffer — leftover cas0 rows can false-positive; HEAD 3×3 / in-queen layer counts are the raster truth. Prefer the slot at `base` if count and map disagree.

**Code map**

| Piece | Where |
|-------|--------|
| Volume | `src/gfx/ShadowCasterVolume.{h,cpp}` |
| Map | `src/gfx/ShadowMap.{h,cpp}` (`last_view_proj`, `last_receiver_vp`, `last_caster`) |
| GPU cull + UBO | `src/gfx/GpuCulling.{h,cpp}`, `shaders/cull_frustum.comp` |
| Cascade loop | `src/gfx/Engine.Render.cpp` |
| Cut pyramids | `src/gfx/Engine.InitializeScene.cpp` |
| Shadow raster / `depthBias` | `src/gfx/Engine.InitializePipeline.cpp` |
| Shade cascade pick | `shaders/pbr.frag` — view-depth slice only (§11.7) |
| Override / lock | `src/AeroBoar.cpp`, `FpsMoveSystem`, `DesktopMoveSystem`, `Camera::pose_locked` |
| P paste | `src/ecs/EditorHotkeySystem.cpp` |

`GpuCullGlobals` std140 (asserted): `skip_frustum_cull` @ 204, `extra_planes` @ 208, sizeof 464.

### 11.3 Closed — off-screen Queen_B (shared UBO)

**Symptom:** Queen off-screen; a long-ish head umbra on the board visible in pose A, gone in pose B after a small rotate. CPU `extra_emit=1` in both.

**Verified pair** (in `configuration.json` now):

- A: pos `(1.15448, 1.79696, 0.49221)` fwd `(-0.84065, -0.541206, 0.0200931)`
- B: pos `(1.14592, 1.80585, 0.611355)` fwd `(-0.834941, -0.547131, -0.0593371)`

Queen_B AABB `(0.0406922..0.573932, 0.185..1.54634, 1.93178..2.46502)`. `L_shadow ≈ (-0.321521, -0.91863, -0.229658)`. Item `ti=62` `batch=19` `base=46` `cap=1`; `cpu_t` matched `ssbo_t`.

| Check | A | B pre-fix | B post-`vkCmdUpdateBuffer` |
|-------|---|-----------|------------------------------|
| Queen off-screen / `R.board` on-screen | yes / yes | yes / yes | yes / yes |
| `R.board` shade cascade | 1 | 1 | 1 |
| CPU `extra_emit` / `ortho6_pass` | 1 / 1 | 1 / 1 | 1 / 1 |
| `head_uvz` in [0,1] | yes (~0.262, 0.701, 0.308) px 537,1435 | yes (~0.269, 0.702, 0.310) px 550,1436 | same |
| GPU instances near Queen | 1 | **0** | **1** |
| HEAD 3×3 | crown ~0.308 | board ~0.13 | crown **~0.310** (= `head_z`) |
| Layer in-queen hi texels (earlier dump) | 50305 | **0** | not re-dumped; 3×3 is enough |

**Cause:** `record()` host-`memcpy`’d one `cull_globals_[fif][pass]` during CB recording. Cas2 then camera prepass/HZB/shade overwrote it **before submit**. Every shadow dispatch read the **last** upload (camera frustum, `skip=0`, `extra_count=0`). Instance SSBO writes were already GPU-timeline (dispatch → shadow draw → next dispatch), so camera cull did not clobber the draw — but the draw list was built with camera planes. Queen survived only when her AABB nicked the camera frustum.

Same class as batch counts (`vkCmdFillBuffer`, not host memset). **Fix:** `vkCmdUpdateBuffer` + GpuOnly `TRANSFER_DST` in `GpuCulling::record`.

**Ruled out** (do not re-open unless new evidence): volume sign/grazing/missing silhouette (CPU emit=1); CPU shadow-ortho fail (`ortho6_pass=1`); `worlds[ti]` translation; batch overflow (`cap=1`); std140 offset mismatch; slope-scaled bias clipping the crown (bias off, same A/B); head UV off the map; Queen transmission (opaque `flags=7`; only `Pawn_Top_*` has transmission); skip-frustum unused as the *CPU* explanation.

An **earlier** A/B pair (not in current JSON) used pos ≈ `(0.536, 2.123, 0.665)` / `(0.523, 2.122, 0.704)` for layer dumps; same bug, different texel grid.

### 11.4 Closed — on-screen pawn umbra was a wrong cut-pyramid near plane

**Symptom:** `bad-shadow.png`, hard cut of a pawn umbra on the board. Not the §11.3 camera-frustum cull (the pawn is on screen).

**Measured view:** default third-person boom, saved as `cameraOverride` pose **P**: pos `(-0.607231, 1.76588, 1.12771)` fwd `(0.404821, -0.520483, -0.751809)`. Splits `(1.019, 2.504, 6.092)`. Census: `shadowProbeTarget=pawns` logs every `Pawn_Body_*` at feet, head-projected board (`far`), and one body-length past that.

**Before the fix (on-screen):** `Pawn_Body_W3/W5/W6` had `si=2` (view-depth past split 1) but `pyr=0`. Worst plane was frustum plane **5** (reverse-Z near, `ndc.z <= 1`) by 4–36 cm. `pyrMask=0` — the board point was in **no** cascade pyramid, while `pbr.frag` sampled cascade 2. `vol=1` and `mapR=1`: the caster was in the (wrong) volume and the point was on that map. Product **P1**.

**Cause:** `cascade_receiver_view_proj` lives in `ShadowCasterVolume.cpp`, whose first glm include was the header **without** `GLM_FORCE_DEPTH_ZERO_TO_ONE`. `glm::perspective` was OpenGL `[-1,1]`. The reverse-Z rewrite `clip.z' = clip.w - clip.z` is only valid for a `[0,1]` matrix (what `Camera::get_projection_matrix` uses). The extracted near plane sat too far out, so receivers between the shade split and that plane sampled a cascade whose pyramid did not contain them. Caster volumes and the ortho fit were built from the same matrix, so they were wrong together.

**Fix:** define `GLM_FORCE_DEPTH_ZERO_TO_ONE` in `ShadowCasterVolume.h` before `<glm/glm.hpp>`. Not a pad.

**After the fix:** on-screen `Pawn_Body_W1`–`W6` are `pyr=1 vol=1 mapC=1 mapR=1` at feet, far, and past. Off-screen pawns still fail a side plane; they are not receivers. Queen_B poses A and B still GPU-count 1 with HEAD 3×3 equal to `head_z` (texel y shifted because the ortho refit).

**If a cut comes back:** freeze the view with **P** / `cameraOverride` (pose P is the boom census). Do not pad. Queen regression: poses A/B, and `record()` must still `vkCmdUpdateBuffer`.

### 11.5 Closed — pawn shadow strength changed with camera angle

**Symptom:** on-screen pawn shadow got lighter or darker as the camera moved. At some angles self-shadow on the body disappeared and the board contact shrank to a small patch. Queen pop (§11.3) stayed fixed.

**Measured** (`shadowProbeTarget=pawns`):

| View | cas0 xy / zspan / old 0.002 bias | cas2 xy / zspan / old 0.002 bias | Example blend |
|------|----------------------------------|----------------------------------|---------------|
| Pose **P** (boom) | 1.20×1.90 m / 7.49 m / **1.5 cm** | 6.66×11.4 m / 15.0 m / **3.0 cm** | `Pawn_Body_W4` feet `blend=0.50` (cas1↔cas2) |
| Pose **G** (low, far) | 1.39×1.57 m / 7.67 m / **1.5 cm** | 8.14×9.56 m / 16.2 m / **3.2 cm** | `W4` feet `blend=0.96` |

Texel size only went from 0.6 mm (cas0) to 3–4 mm (cas2). The compare bias did not: `shadowParams.w = 0.002` was a fixed fraction of each cascade’s **light-space depth range**, so the same pawn was pushed 1.5 cm toward the light in cas0 and 3 cm in cas2. The blend band (`max(split*0.18, 0.35 m)`) mixes those two maps. View-depth, and therefore the mix, follows the camera. A pawn’s contact along this light is only a few centimeters, so a 2–3 cm bias eats self-shadow and most of the board umbra. Raster slope bias (`constant -1.25`, `slope -1.5`, reverse-Z, no depth clamp) made steep faces worse: it scales with pixels of slope and can push fragments to `z<0`, which clips them so they never store.

**Fix (not a volume pad):**

- Shadow pipeline depth bias **off** (`shadows.depthBias=false`; factors stay zero in `Engine.InitializePipeline.cpp`).
- `FrameConstants.shadowTexelWorld` is meters per texel per cascade (XY extent / resolution).
- `pbr.frag` `receiver_offset_m` moves the receiver toward the light by `texel * sqrt(1-ndl²) / ndl` (`ndl` clamped to 0.2). Face-on stays under a texel; grazing is a few texels of **that** cascade. It does not grow with `zspan`. `shadows.bias` (0.002) is no longer uploaded.

**After:** Queen poses A and B still GPU-count 1. HEAD 3×3 matches `head_z` (A ≈ 0.306, B ≈ 0.307). On-screen pawn census at P and G stays `pyr=1 vol=1 mapC=1 mapR=1`.

**Follow-up (lit-surface pattern):** the first offset was `texel * slope(NdotL)` using the **normal-mapped** `NdotL`, and it was smaller than the PCF footprint (~1.25 texels plus the hardware 2×2). Lit board and piece tops are in the shadow map, so some taps compared slightly behind the surface and the 8-tap average came back as 0.875 / 0.75 instead of fully lit. The normal map made that a texture-shaped pattern, and it changed (popped) as the camera moved the texel grid or the cascade blend. The slope term uses the geometric normal only. Bias is `texel * max(0.75 * slope, 0.2)`. The compare sampler is nearest, and the Vogel disk is half a texel. A hardware 2×2 plus a 1.25-texel disk turned `Pawn_Body_W4`’s self-shadow into a gray that flickered (pose **U**: cascade 1, blend 0, map stable across frames, pawn at the bottom edge of the view).

**Y translation (fixed):** `proj[1][1] *= -1` left the ortho Y offset in place, so a window that is not centered on zero mapped receivers off the texture. After also negating `proj[3][1]`, poses **S**, **T**, and **U** have no on-screen pawn with `pyr=1` and `mapR=0`. `W4`’s shadow U changed (S: 0.75 → 0.47) and X did not. Between S and T the UV still steps by whole texels (~37, ~5) on the 2 mm grid.

**Follow-up (self-shadow pops between two nearby poses):** poses **C** and **D** in `cameraOverride`. `Pawn_Body_W4` is the on-screen pawn. Its feet stay in cascade 1, but the blend toward cascade 2 is **0.97** at C (shadow gone) and **0.68** at D (shadow visible). The pawn is in both volumes (`vol=6`). Cascade 2’s texel is ~4.5 mm, so the two-texel bias is ~9 mm and lifts the self-shadow; cascade 1’s bias is ~4 mm and keeps it. `mix` was dissolving the good sample into the lifted one. In the split band the shader now keeps `min` of the two compares (darker wins). Past the split only the coarser map remains, so detail smaller than that texel can still drop.

**Poses E / F** (shadow looked present, then missing): the crosshair pawn is `Pawn_Body_B2`. It is cascade 2 in both, 2.5 m inside that volume, and the contact texel stores the pawn (F: 0.417 vs board 0.386). That shadow is in the map. The pawn nearest the barbarian is `Pawn_Body_W4` (bottom of this frame). Cascade 2’s tightest plane is **-9 mm** at E (still inside the 2 cm test) and **-45 mm** at F (culled). Its contact is cascade 1 in both, and that map still stores the pawn (F: 0.212 vs board 0.162). Do not pad cascade 2 to keep W4.

### 11.6 Closed — cascade buffer race, off-map sample

**Buffer race.** Each cascade culls into one opaque instance buffer and one indirect-command buffer, then draws. The next cascade's `GpuCulling::record` overwrites both. The shadow render pass has no external subpass dependency. `record` waited only on compute, so a GPU that overlaps the next dispatch with the previous draw could shade cascade 0 or 1 from a list the following cull had already started replacing. The depth prepass has the same shape: draw, then the Hi-Z cull refills the same buffers.

**Fix:** the barrier at the start of `record` now sources `VERTEX_SHADER` and `DRAW_INDIRECT` as well as compute, with `INDIRECT_COMMAND_READ` in the source access. That orders the previous draw's reads ahead of `vkCmdUpdateBuffer` / `vkCmdFillBuffer`, and the existing transfer→compute barrier orders those ahead of the refill. The handoff after the last cascade (`Engine.Render.cpp`) includes draw-indirect too.

**Off-map sample.** `sample_shadow` treated "both selected cascades missed the map" as a cue to sample cascade 2. A receiver offset can push a finer-map UV just outside `[0,1]` while the coarse map still contains that point, so the pixel took the wrong depth. Superseded by §11.7: the shader walks cascades from fine to coarse and keeps the first map that contains the receiver.

### 11.7 Closed — coarse cascade drawn over the table

**Symptom:** shadows stayed present, but the texel size was the coarse map. The table looked like it was shaded from cascade 2.

**Cause:** two selections stacked. PSSM `kLambda = 0.7` packs resolution at the clip near plane. That plane is centimetres of empty space (`Camera` near is clamped, then `worldScale`d), and the table is metres along the view, so cascade 0 covered air at well under a millimetre and the table fell in cascade 1 or 2 (about 2–5 mm). Inside the split band the shader also took `min` of the selected map and the next coarser one, so the coarse texel was displayed for the last 0.35 m of the finer cascade as well.

**What the force-finest test showed.** Sampling only cascade 0 made the texel small where that map covered the surface, and the shadow vanished as the camera turned. The table's view-depth was past cascade 0, so the slice that owned it was a wide far map (pixelated). Forcing the near map samples a rectangle the table is outside of, and the compare misses.

Starting the slices at the nearest AABB corner made this worse. Anything closer than that corner was still labeled cascade 0, but the map began at the corner, so those receivers were off the map and the shadow dropped.

**Fix:** cascade 0 runs from the clip near plane to just past the view-depth of the scene center (`focus`, then `max(focus + 0.5 m, focus * 1.35)`). The subject, and everything between it and the lens, is inside that map. The far face of the map is the subject, so the texels stay small. Past that, cascades 1 and 2 split the remainder out to the farthest scene corner. `sample_shadow` uses the view-depth slice only. |