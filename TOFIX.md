# TOFIX

## [BUG] QR decode fails — quirc ECC failure on valid image

**Branch:** `fix/qr-ecc-contrast`

**Root cause confirmed (PC decode of notapro_frame.bin):**

WeChatQRCode (OpenCV) decodes the frame instantly. The image is fine.
quirc fails because:

1. **Module size is only ~3.5px** at 400x240. quirc's Reed-Solomon decoder
   needs sufficient contrast per module to sample correctly. At 3.5px/module
   it samples too close to module edges and gets bit errors.

2. **Payload is exactly 376 bytes = v13-Q at capacity.** v13-Q provides ~94
   bytes of ECC correction headroom. At 3.5px/module with 3DS camera noise
   the bit error count exceeds this threshold, so ECC cannot recover.

3. **The fix:** bicubic-upsample the detected QR ROI to ~700x700 before
   feeding to quirc. At 8-10px/module the RS decoder has no trouble.
   This is what WeChatQR does internally (super-resolution pass).

**Required change in qr.c (`fix/qr-ecc-contrast`):**
- After finder pattern detection (quirc_end), extract the bounding box
  of the detected code from `quirc_code.corners`.
- Crop that ROI from `s_grey1` with a small margin.
- Bicubic-upsample the ROI to a fixed `quirc_resize`'d buffer of 700x700.
- Feed the upsampled ROI to a dedicated `qrc_roi` quirc instance.
- Keep the 1x/2x passes as fallback.

**Payload confirmed:**
```json
{"avecPageConnexion":false,"jeton":"91472F67...","login":"D733111B...","url":"https://0260008t.index-education.net/pronote/mobile.eleve.html"}
```
Login field is a hex UUID (not plaintext username). JSON parser in main.c
needs to handle this correctly — `login` maps to the username field.
