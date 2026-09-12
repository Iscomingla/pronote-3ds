# notApro — Quick Start

## Build & install

```bash
# 1. Build
make clean && make

# 2. Copy to SD card
cp pronote-3ds.3dsx /path/to/sd/3ds/pronote-3ds.3dsx

# 3. Launch from Homebrew Launcher
```

## Use it

| Button | Action |
|--------|--------|
| UP / DOWN | Switch field |
| **R** | **Scan QR code** (outer camera) |
| A | Edit field (keyboard opens pre-filled) |
| Y | Clear field |
| X | Login |
| START | Exit |

## Login steps

1. Press **R** → point outer camera at Pronote QR code
2. Username + jeton fill automatically (jeton up to 224 chars)
3. Navigate to PIN → press **A** → enter 4 digits
4. Press **X** to login

## Common issues

| Problem | Fix |
|---------|-----|
| Build fails: `DEVKITARM not set` | `export DEVKITARM=/opt/devkitpro/devkitARM` |
| App crashes on launch | Check CFW / Homebrew Launcher setup |
| QR not detected | Ensure good lighting, hold camera ~20 cm from code |
| "Invalid QR format" | QR must contain JSON with `login` and `jeton` keys |
