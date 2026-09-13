# TOFIX

## [BUG] QR decode fails — quirc RS decoder too weak for this code

**Branch:** `fix/qr-ecc-contrast`

**Status:** Definitively diagnosed. quirc cannot decode this QR. Replacement needed.

### What we know

- **The image is fine.** WeChatQRCode decodes it instantly from the raw frame.
- **Adaptive threshold is fine.** `cv2.adaptiveThreshold(grey, 255, ADAPTIVE_THRESH_GAUSSIAN_C, THRESH_BINARY, 15, 5)` produces a clean binary image that WeChatQR decodes.
- **OpenCV's built-in QR detector also fails** — same RS decoder class as quirc.
- **Upscaling does not help quirc.** Tested bilinear and nearest-neighbour at 300, 400, 500, 600, 700px — quirc and OpenCV fail at all sizes. WeChatQR succeeds at all sizes.
- **Root cause:** The Pronote QR is v13-Q at **exactly 376 bytes capacity** (payload fills it to the byte). v13-Q provides ~94 bytes of ECC correction. The 3DS camera at 400x240 produces ~3.5px/module; even with upscaling, quirc's RS implementation generates more bit errors than that budget allows. WeChatQR uses a neural-network super-resolution pass before RS — quirc has no such pipeline.

### Required fix

**Replace quirc with zxing-cpp** (`zxing-cpp/zxing-cpp` on GitHub).

zxing-cpp is a C++17 port of ZXing with a significantly stronger Reed-Solomon implementation. It:
- Ships as a small set of headers + source files, no external deps
- Compiles on ARM with devkitARM (C++17 supported since devkitARM r55)
- Has been used in other 3DS homebrews (e.g. Checkpoint)
- Can accept a raw greyscale buffer directly via `ImageView`

### Integration plan

1. Vendor `zxing-cpp` in `lib/zxing-cpp/` (only the `core/src/` subtree needed, ~80 files)
2. Add `lib/zxing-cpp/core/src` to `SOURCES` and `INCLUDES` in Makefile
3. In `qr.c`: keep the camera thread unchanged. Replace the quirc decode call with:
   ```cpp
   auto hints = DecodeHints().setFormats(BarcodeFormat::QRCode);
   auto result = ReadBarcode({grey_buf, width, height, ImageFormat::Lum}, hints);
   if (result.isValid()) { /* copy result.text() to out_buf */ }
   ```
4. Keep `qrc_detect` (quirc) for the fast bounding-box detection pass only — it reliably finds the finder patterns even when it can't decode. Use the bbox to crop+upsample for zxing-cpp.
5. Remove `qrc_roi` (quirc decode instance) entirely.

### Payload confirmed

```json
{"avecPageConnexion":false,"jeton":"91472F67A2174D0A...","login":"D733111BDFB5BE004343EE8F18FD0E1F","url":"https://0260008t.index-education.net/pronote/mobile.eleve.html"}
```

`login` is a hex UUID, not a display name. `main.c` JSON parser handles it correctly already.
