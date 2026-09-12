# TOFIX

- [ ] QR decode still failing (DATA_ECC) after mutex + scale fixes:
      Buffer errors gone. Version now stable at v13/v14 but flips between them,
      meaning quirc's grid alignment is off (perspective correction error).
      Finder pattern corners not detected precisely enough -> module sampling
      lands between cells -> bit errors exhaust ECC capacity.
      Fix: apply 3x3 unsharp mask to greyscale image before quirc to sharpen
      finder pattern edges, improving corner detection accuracy.
