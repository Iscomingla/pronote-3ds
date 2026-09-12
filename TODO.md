# TODO — notApro

Planned features, roughly in implementation order.

## 🔐 Authentication

- [x] **PIN decryption of jeton** — decrypt the jeton from the QR code using the 4-digit PIN (AES-CBC, key = MD5(pin), IV = all zeroes — see PRONOTE protocol)
- [ ] **HTTP login request** — send decrypted credentials to the Pronote API endpoint
- [ ] **Session persistence** — save session cookie to SD card so re-login isn't needed every time
- [ ] **Secure credential storage** — encrypt saved credentials at rest

## 📡 Network

- [ ] **Implement `network.c`** — replace the current stub with real HTTP via libctru's `httpcInit`
- [ ] **Follow redirects** — handle Pronote's redirect chain after login
- [ ] **Retry logic** — retry failed requests up to 3 times with a delay
- [ ] **Response caching** — cache responses for ~5 minutes to reduce load

## 📊 Data Display

- [ ] **Parse Pronote API response** — extract grades, timetable, homework from JSON/HTML
- [ ] **Display grades** — formatted list with subject, grade, and max
- [ ] **Display timetable** — day view with time slots
- [ ] **Display homework** — list with due dates
- [ ] **Auto-refresh** — periodic background update

## 🖥️ UI

- [x] **Switch to citro2d/citro3d** — `ui.c`/`ui.h` layer wrapping C2D/C3D; GPU-accelerated, dual-screen, system font
- [ ] **Camera preview in QR scanner** — blit camera frames to a GPU texture + draw crosshair in `qr.c` (skipped for now; requires C3D texture upload while camera DMA is active)
- [ ] **Dual-screen layout** — data on top screen, nav/controls on bottom (login screen already uses dual-screen)
- [ ] **Scrollable output** — scroll through content longer than one screen
- [ ] **Loading screen** — progress bar during network requests
- [ ] **Status bar** — persistent connection/error status
- [ ] **Settings menu** — language, timeout, cache duration

## 🌍 Misc

- [ ] **French/English UI strings** — i18n support
- [x] **File-based logging** — write debug log to SD card (`sdmc:/3ds/notApro/log.txt`)
- [ ] **Error recovery** — user-friendly messages for network/auth failures
- [ ] **CIA packaging** — proper metadata + banner for installable CIA
