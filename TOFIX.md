# TOFIX

## Camera: DMA cache coherency bug

Symptom: 1-in-2 rows/pixels black, small white column, full black in emulator.

Cause: CAMU DMA writes directly to physical memory bypassing CPU cache. CPU reads stale cached zeros instead of DMA data.

Fix: call `GSPGPU_FlushDataCache(cam_buf, CAM_BUF_SIZE)` before `CAMU_SetReceiving`, then `GSPGPU_InvalidateDataCache(cam_buf, CAM_BUF_SIZE)` after `svcWaitSynchronization` returns and before reading the buffer.
