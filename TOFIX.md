# TOFIX

## [BUG] Camera sysmodule kernel panic on QR scan launch

**Branch:** `feat/citro2d-ui-step1`

**Symptom:** Prefetch abort / kernel panic in `camera` process (`0004013000001602`) immediately when QR scanning is triggered. Happens on both the old and current code.

**Crash signature:**
- `pc=0xfff1c848`, `dfsr=0x000018ff` (debug event fault), camera sysmodule kernel thread
- `r4=0xffffffff`, `r6=0xffff9000` (invalid handle / kernel error code on stack)
- Identical crash across two separate runs — deterministic

**What was tried:** Removing the `ui_exit()`/`ui_init()` cycle around `qr_scan()` (previously suspected of disturbing GSP/GPU state during DMA). Panic persists — root cause is elsewhere.

**Suspected cause:** citro2d/citro3d may be holding a GSP or memory mapping that conflicts with the camera sysmodule's DMA setup, even when citro2d is just idle (no frames submitted). May need to explicitly call `C3D_FrameEnd` / pause the GPU before `camInit()`, or investigate whether `CAMU_SetReceiving` requires the CPU-side GPU to be fully quiesced.

**To investigate:**
- Does the panic happen if citro2d is never inited (console-only fallback)?
- Does calling `gspWaitForVBlank()` + a dummy `C3D_FrameBegin/End` before `camInit()` help?
- Check if `GSPGPU_FlushDataCache` before `CAMU_SetReceiving` is needed (was in the old RESET_ONESHOT version, removed in RESET_STICKY version)
