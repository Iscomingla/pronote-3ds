# notApro — Pronote on 3DS

A Nintendo 3DS homebrew app to access Pronote (French school management system) using QR code authentication.

> **Status:** QR scanning and PIN entry work. Actual login (PIN decryption + HTTP request) is not yet implemented — see [TODO.md](TODO.md).

## Requirements

- **devkitPro** with devkitARM: https://devkitpro.org/wiki/Getting_Started
- **libctru** (included with devkitPro)
- **quirc** (bundled in `lib/quirc/`)

```bash
# Required devkitPro packages
pacman -S devkitARM 3ds-dev
```

## Build

```bash
make        # Build .3dsx
make clean  # Clean build artifacts
```

Output: `notApro.3dsx`

## Install on 3DS

Copy `notApro.3dsx` to your SD card:
```
/3ds/notApro.3dsx
```
Launch from Homebrew Launcher.

## Usage

### How Pronote QR Login Works

1. **Generate QR code** in your Pronote app (mobile or web) — Settings → QR Login
2. **Scan it** with the 3DS camera inside notApro
3. The QR contains your encrypted `login` and `jeton` fields
4. **Enter the 4-digit PIN** shown alongside the QR code to decrypt your credentials
5. Press **X** to connect

### Controls

| Button | Action |
|--------|--------|
| **UP/DOWN** | Navigate between fields |
| **A** | Edit field / Open camera to scan QR |
| **B** (in scanner) | Cancel scan |
| **Y** | Clear field |
| **X** | Login |
| **START** | Exit |

### Fields

- **Username** — pre-filled automatically after QR scan
- **Jeton** — scanned from QR code (up to 255 chars, no manual entry needed)
- **PIN Code** — 4-digit code shown alongside the QR code

## Technical Details

- Uses **libctru** for graphics, input, and camera
- Uses **quirc** (`lib/quirc/`) for QR code decoding from camera frames
- Camera captures at 400×240 in YUV422; Y channel extracted for quirc
- Pronote QR codes contain a JSON payload: `{"login":"...","jeton":"...","url":"..."}`
- PIN decryption and HTTP login are not yet implemented — see [TODO.md](TODO.md)

## License

MIT — not affiliated with Index-Education or Pronote.
