# Port of VCCoop's robustness layer to SACoop (0.49.0-vcport, NET_VERSION 19)

Both players must run the **same build** (the wire format of vehicle and ped messages gained a `time` field).

## Why

VCCoop is smooth because remote entities are *interpolated from timestamped states* and *placed right after the
game's physics step*. SACoop used to place them after the whole frame was rendered and extrapolate/snap them, ran at
60 FPS with framerate corrections, and mirrored fewer mission opcodes. This port brings the VC design over.

## Modules (each has a kill-switch in `sacoop.ini`, section `[SACoop]`)

| Key (default)             | What it does | If it misbehaves |
|---------------------------|--------------|------------------|
| `ImagesParSeconde=30`     | 30 FPS: the original game timing, the Framerate-Vigilante-style corrections in `fps.cpp` are skipped (`maxFps <= 30`). | try 60 again |
| `Interpolation=1`         | Hooks the `CGame::Process` call inside `CIdle` (0x53E920 → 0x53BEE0, found at run time, must be unique). Adds clock offsets per sender, a track of the last 10 states per entity, and Hermite/linear sampling `DelaiInterpolation` ms (110) in the past. | `0` = old behaviour (nothing hooked unless `GardeFou=1`) |
| `PlacementPietons=1`      | Remote players and mission peds (guest side) are placed after physics: position + heading written, then `CEntity::UpdateRwMatrix`/`UpdateRwFrame` (0x446F90 / 0x532B00). The GoToPoint task stays, so the walk animation still plays. | `0` = glide towards target as before |
| `PlacementVehicules=1`    | Remote vehicle copies are placed before **and** after the physics step (occupants and passenger camera stay in step). | `0` = old `UpdateCopy` |
| `GelerCopies=1`           | Sets `bDontApplySpeed` (CPhysical flags +0x40, bit 13) on copies so local physics cannot push them; cleared when the local player drives it or updates go stale (1.5 s). | `0` |
| `GardeFou=1`              | Before each `CGame::Process`, vehicles and peds with NaN / absurd matrix, position or speed get their last sane pose back (or are laid flat), speeds zeroed, logged as `sanitizer: ...` (first 60). Targets the world-sector crash seen on VC guests. | `0` |
| `MiroirEtendu=1`          | ~120 more mission opcodes mirrored from host to guest (texts, blips, camera/fade, mission audio, cutscene helpers, roads, garages, objects, pickups). Blip modifications no longer overwrite the creation entry kept for late joiners. | `0` |

## Bisect order if the build is worse than before

1. `ImagesParSeconde=30` alone (copy the old DLL back) — is it already better?
2. New DLL with all new keys `=0` → must behave like the old build (except the 30 FPS default).
3. Turn on one key at a time in this order: `GardeFou`, `Interpolation` + `PlacementVehicules`, `GelerCopies`,
   `Interpolation` + `PlacementPietons`, `MiroirEtendu`.
4. Send `sacoop.log` from **both** players. Relevant lines: `interpolation: hooked ...` (or `not found ...`),
   `sanitizer: ...`, mirror warnings.

## Not ported (on purpose)

Ragdoll sync (VC bone tags do not map to SA), on-screen timers/counters (need script variables), model-loading ops,
`CLEAR_AREA`, trains, roadblocks, per-frame text-draw commands, VC cutscene-object opcodes (NOPs in SA).

## Build

`build-dll.cmd` finds Visual Studio (2019–2026), builds `build\dinput8.dll` (Win32, /MT) and writes `build\build.log`.
Copy that DLL over `dinput8.dll` in the San Andreas folder, on both machines.
