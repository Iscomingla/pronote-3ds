# TOFIX

## [BUG] Camera sysmodule kernel panic a few seconds after entering QR scan

**Branch:** `fix/camera-linear-alloc` (latest attempt)

**Symptom:** Prefetch abort / kernel panic in `camera` process (pid 0004013000001602),
a few seconds after QR scan is triggered — not immediately. Stable on emulator.

**Crash dump (real hardware):**
- Exception: prefetch abort, fault status `Debug event` (`dfsr 0x18ff`)
- Faulting process: `camera`, `pc fff1c848` (kernel space)
- `ifsr 0x00000002` (precise external abort), `far ffbffeff`

**Fixes attempted (none resolved it):**
1. Buffer size: `calloc` → `linearAlloc` (DMA requires linear heap) — still crashes
2. GSP conflict: `ui_suspend()`/`ui_resume()` around camera session to release citro3d's GSP hold — still crashes
3. `C2D_TargetClear` order, missing `ui.h` declarations, duplicate `ui_get_target` — resolved separately

**Current hypothesis:** The crash happens a few seconds in, not immediately, suggesting a
repeated or re-entrant call to the camera sysmodule is the trigger — possibly
`CAMU_SetReceiving` being called again inside the capture loop while a transfer is
still in flight, causing the sysmodule to fault on its next DMA attempt.

**To investigate:**
- Remove the re-arm call (`CAMU_SetReceiving` inside the loop) and only re-arm after
  the previous event has been consumed and cleared
- Check whether `GSPGPU_InvalidateDataCache` without citro3d alive still works
  (we no longer hold the GSP handle after `ui_suspend`)
- Consider using `RESET_ONESHOT` instead of `RESET_STICKY` and re-creating the event each frame
