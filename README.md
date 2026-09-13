# notApro — Pronote on 3DS

A Nintendo 3DS homebrew app to authenticate with Pronote using a QR code jeton + 4-digit PIN.

## Requirements

- **devkitPro** with devkitARM — https://devkitpro.org/wiki/Getting_Started
- **libctru** and **citro2d** (included with devkitPro)

No extra pacman packages needed. quirc is vendored under `lib/quirc/`.

```bash
pacman -S devkitARM 3ds-dev
```

## Build

```bash
make clean && make
```

Produces `pronote-3ds.3dsx`. Copy it to `/3ds/pronote-3ds.3dsx` on your SD card and launch via Homebrew Launcher.

---

## Login setup

The in-app QR scanner is currently deferred (see TODO). Instead, generate your
`user.json` on a computer and copy it to your SD card — this takes about a minute.

### Step 1 — Generate the Pronote QR code

1. Open Pronote on your phone or computer
2. Go to **Settings → QR Code Login** (or the equivalent in your school's Pronote)
3. A QR code and a 4-digit PIN are shown together — **note the PIN**, you will need it

### Step 2 — Decode the QR code on your computer

Scan or screenshot the QR code, then decode it with any QR reader.
The payload is a JSON string:

```json
{"login":"<hex string>","jeton":"<hex string>","url":"https://..."}
```

Quick ways to decode it:

**Option A — zbar (Linux/macOS)**
```bash
zbarimg --raw qr.png
```

**Option B — Python**
```python
from pyzbar.pyzbar import decode
from PIL import Image
print(decode(Image.open('qr.png'))[0].data.decode())
```

**Option C — Online**  
Upload the screenshot to https://zxing.org/w/decode.jspx or any QR decoder site.

### Step 3 — Create `user.json`

Copy only `login`, `jeton`, and `url` from the decoded payload:

```json
{
  "login": "<value from QR>",
  "jeton": "<value from QR>",
  "url":   "<value from QR>"
}
```

Save it as `user.json`.

### Step 4 — Copy to SD card

```
SD card:
  /3ds/notApro/user.json
```

Create the `notApro` folder if it does not exist. The app also writes its log to
`/3ds/notApro/log.txt`.

### Step 5 — Launch and enter PIN

1. Launch **notApro** from Homebrew Launcher
2. `user.json` is loaded automatically — you will see your username and "Loaded ✓"
3. Press **A** to enter your 4-digit PIN (the one shown alongside the QR code)
4. Press **X** to login

> The QR code and PIN are only valid for a limited time (usually 10 minutes).
> Generate a new one if login fails.

---

## Controls

| Button | Action |
|--------|--------|
| A | Enter PIN |
| Y | Clear PIN |
| X | Login |
| START | Exit |

---

## Project structure

```
src/
  main.c      — UI, file loading, input loop
  network.c   — HTTP stub (WIP)
  ui.c        — citro2d rendering layer
include/
  ui.h
  network.h
lib/quirc/    — Vendored quirc QR library (used by the deferred scanner)
```

## Log file

The app writes a timestamped session log to `sdmc:/3ds/notApro/log.txt`.
Useful for debugging if login fails.

## Disclaimer

Not affiliated with Index-Education or Pronote. Use at your own risk and follow your school's policies.
