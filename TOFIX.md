# TOFIX

## [BUG] Camera sysmodule kernel panic on QR scan launch

**Branch:** `feat/citro2d-ui-step1`

**Symptom:** Prefetch abort / kernel panic in `camera` process immediately when QR scanning is triggered.

**Suspected cause:** citro2d/citro3d may be holding a GSP or memory mapping that conflicts with the camera sysmodule's DMA setup.

---

## [BUG] Black screen on launch, no interaction, wifi cut

**Branch:** `feat/citro2d-ui-step1`

**Symptom:** App launches to a black screen with no input response. Wireless connection drops.

**Root causes identified:**
1. `C2D_TargetClear` called after `C2D_SceneBegin` — must be before.
2. `C2D_FontLoadSystem` return value not checked — NULL font crashes on some firmwares.
3. `LIBDIRS` missing citro2d/citro3d lib path — linker may pick wrong lib versions.

**GDB debbuging result:**
```
Ignoring packet error, continuing...
warning: unrecognized item "timeout" in "qSupported" response
Remote replied unexpectedly to 'vMustReplyEmpty': PacketSize=400;qXfer:features:read+;qXfer:osdata:read+;QStartNoAckMode+;QThreadEvents+;QCatchSyscalls+;vContSupported+;swbreak+;multiprocess+
```
