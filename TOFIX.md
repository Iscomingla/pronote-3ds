# TOFIX

## [BUG] QR decode persistent ECC failure — needs frame dump to diagnose

**Branch:** `fix/qr-ecc-contrast`

**Status:** Blocked. 15+ sessions of image processing changes (contrast stretch,
bilinear upscale, sharpening, multi-threshold) all fail identically.
quirc finds the code every time (v13/v14, stable corners) but DATA_ECC
on every frame, normal and flipped. The image processing layer is not
the problem.

**What we know:**
- Finder patterns detected reliably and consistently (TL/TR/BR/BL corners stable)
- Version oscillates between v12/v13/v14 on the same physical QR — quirc's
  module-size estimator is getting different answers each frame, meaning the
  perspective transform varies. This is the root cause of ECC failure: if
  the module count estimate is off by 1, every cell is sampled at the wrong
  position and the entire data region is corrupted.
- All image processing tricks (contrast, sharpen, upscale, threshold sweep)
  cannot fix a wrong module count — that's determined by the finder pattern
  geometry alone.

**Suspected root cause:**
  The 3DS outer camera `SIZE_CTR_TOP_LCD` (400x240) applies a hardware
  downscale from the native 512x384 sensor. This introduces sub-pixel
  aliasing at finder pattern edges that makes the 7-module finder pattern
  look like 6 or 8 modules to quirc's run-length scanner. Result: wrong
  version estimate, wrong grid, ECC failure.

**Required before next code attempt:**
  Dump one raw RGB565 frame to `/sdmc/notapro_frame.bin` (400x240x2 bytes)
  when a code is detected, then decode it on PC with a Python quirc wrapper
  or ZXing. This will tell us:
  1. Is the frame data actually a valid QR when decoded by a proper decoder?
  2. If yes: quirc is broken on this image. If no: the camera image is
     genuinely undecodable (wrong focus, overexposure, etc.).

**To add frame dump (one-liner in qr.c after quirc_count > 0):**
  ```c
  FILE *f = fopen("/sdmc/notapro_frame.bin", "wb");
  if (f) { fwrite(s_frame_buf, 1, CAM_BUF_SZ, f); fclose(f); }
  ```
  Then on PC: `python3 -c "import cv2,numpy; f=open('notapro_frame.bin','rb').read(); img=numpy.frombuffer(f,numpy.uint16).reshape(240,400); cv2.imwrite('frame.png', cv2.cvtColor(img.view(numpy.uint8).reshape(240,400,2), cv2.COLOR_BGR5652BGR))"`
  Then run ZXing or pyzbar on frame.png.

**Alternative approach if dump confirms image is good:**
  Switch from `SIZE_CTR_TOP_LCD` to `CAMU_SetDetailSize(SELECT_OUT1,
  192, 192, 160, 96, 352, 288, CONTEXT_A)` — this crops the central
  192x192 from the native 512x384 sensor without downscaling. For a v13
  QR (69 modules) held ~30cm away this gives ~2.8px/module with NO
  interpolation blur. Combined with the existing ROI upscale to 256x256,
  that's 3.7px/module with zero aliasing artifacts.
