# notApro — Pronote on 3DS

A Nintendo 3DS homebrew app to authenticate with Pronote using a QR code jeton + 4-digit PIN.

## Requirements

- **devkitPro** with devkitARM — https://devkitpro.org/wiki/Getting_Started
- **libctru** (included with devkitPro)

No extra pacman packages needed. quirc is vendored under `lib/quirc/`.

## Build

```bash
make clean && make
```

Produces `pronote-3ds.3dsx`. Copy it to `/3ds/pronote-3ds.3dsx` on your SD card and launch via Homebrew Launcher.

## Usage

### Login flow

1. Press **R** to open the QR scanner
2. Point the **outer camera** at your Pronote QR code
3. Username and jeton are filled automatically
4. Navigate to **PIN Code** and press **A** to enter your 4-digit PIN
5. Press **X** to login

### Controls

| Button | Action |
|--------|--------|
| UP / DOWN | Navigate fields |
| A | Edit field / Scan QR (on jeton field) |
| Y | Clear field |
| R | Open QR scanner |
| X | Login |
| START | Exit |

### QR code format

The app expects the Pronote QR payload to be JSON:

```json
{"login":"username","jeton":"<up to 224 chars>"}
```

The jeton field supports up to **224 characters** (Pronote's maximum).

## Project structure

```
src/
  main.c      — UI and input loop
  qr.c        — Camera capture + quirc QR decoder
  network.c   — HTTP stub (WIP)
include/
  qr.h
  network.h
lib/quirc/    — Vendored quirc QR library
```

## Building a CIA

See `ADVANCED.md` for CIA packaging instructions.

## Disclaimer

Not affiliated with Index-Education or Pronote. Use at your own risk and follow your school's policies.
