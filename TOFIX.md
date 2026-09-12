# TOFIX

- [ ] QR decode still failing after multi-scale + adaptive threshold attempt:
      - 4x/3x scales never detect codes (too small for finder pattern detection)
      - Adaptive threshold causes FORMAT_ECC (error 3) by corrupting format strips
      - Mutex held during processing causes camera DMA buffer errors
      Fix:
        1. Copy frame under mutex, release before processing
        2. Drop adaptive threshold — use raw greyscale, let quirc Otsu run
        3. Keep only 2x scale (only one that detects); add 1x (full 400x240) as fallback
