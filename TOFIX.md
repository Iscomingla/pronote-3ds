# TOFIX

## [BUG] Camera sysmodule kernel panic on QR scan launch

**Branch:** `feat/citro2d-ui-step1`

**Symptom:** Prefetch abort / kernel panic in `camera` process (`0004013000001602`) immediately when QR scanning is triggered.

**Crash signature:**
- `pc=0xfff1c848`, `dfsr=0x000018ff` (debug event fault), camera sysmodule kernel thread
- `r4=0xffffffff`, `r6=0xffff9000` (invalid handle / kernel error code on stack)
- Deterministic — happens every run

**Root cause (from PR #20):** Three separate issues compounding:
1. citro3d holds a GSP session the camera sysmodule cannot share — fixed by `ui_suspend()`/`ui_resume()` around `camInit()`/`camExit()`
2. `calloc` gives standard heap; camera DMA requires linear heap — fixed by `linearAlloc`/`linearFree`
3. RESET_STICKY + immediate re-arm after `svcClearEvent` does not wait for DMA to finish, causing the sysmodule to fault — fixed by RESET_ONESHOT with a fresh event per frame

**Status:** Fix merged in PR #20. Needs hardware verification.
