# Camera preview — remaining issues

Symptom: image is recognisable (looks like a photo) but only 1 in every 2 pixels has colour — the other is black. Classic interleaving bug.

Likely cause: the camera buffer is being read as u16 but the DMA transfer may deliver the data as packed bytes with a different alignment, so every other pixel lands on a wrong address. Or bufSize from CAMU_GetMaxBytes is half what we expect, causing the receive to only fill half the buffer.

Things to investigate/fix:

Check bufSize value — CAMU_GetMaxBytes returns bytes per line (not total). The total buffer passed to CAMU_SetReceiving should be bufSize * CAM_HEIGHT, not CAM_WIDTH * CAM_HEIGHT * sizeof(u16). This mismatch likely causes partial fills.
CAMU_SetTransferBytes line pitch — the last argument to CAMU_SetReceiving is the line size in bytes ((s16)bufSize), but CAMU_SetTransferBytes takes (totalBytes, width, height) — double-check the devkitPro example uses bufSize * HEIGHT as the total, not just bufSize.
Try memcpy to a u8* first — rule out u16* pointer aliasing causing the every-other-pixel skip.
Alternative: switch framebuffer format to RGB565 — call gfxInit(GSP_RGB565_OES, GSP_BGR8_OES, false) instead of gfxInitDefault() in main.c, then write the camera u16 buffer directly with memcpy without any pixel conversion, and drop the blit loop entirely. This is the simplest possible path and would also be much faster.

Option 4 is probably the cleanest fix — avoid the blit math entirely by making the framebuffer format match the camera format.