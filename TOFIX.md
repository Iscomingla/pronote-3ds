# TOFIX

- [ ] QR decode fails with QUIRC_ERROR_DATA_ECC (error 4) on Pronote v13/v14 QR codes:
      2x2 downsample to 200x120 still too low resolution (~2.9px/module for v14).
      Fix: try multiple downsample scales (4x4, 3x3, 2x2) per frame + local
      adaptive threshold instead of global Otsu.
