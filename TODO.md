# TODO — notApro

Planned features, roughly in implementation order.

## 🔐 Authentication

- [ ] **PIN decryption of jeton** — decrypt the jeton from `user.json` using the 4-digit PIN (AES-CBC, key = MD5(pin), IV = all zeroes — see PRONOTE protocol)
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
- [ ] **Display timetable** — day view with time slots and informations about cancelled/moved courses, missing teachers, ...
- [ ] **Display homework** — list with due dates and status (`fait` or `à faire`)
- [ ] **Homework filters** — `fait`, `à faire` or specific subject
- [ ] **Display grades** — formatted list with subject, grade, and max; show `Notes` and `Compétences`
- [ ] **Informations and Discusions** — list of communications, clickable to view the full message
- [ ] **Auto-refresh** — periodic background update
- [ ] **Main menu** — summary of all the content: next 3 `à faire` homework; most recent `Informations et sondages`

## 🖥️ UI

- [x] **Switch to citro2d/citro3d** — `ui.c`/`ui.h` layer wrapping C2D/C3D; GPU-accelerated, dual-screen, system font
- [ ] **Dual-screen layout** — data on top screen, nav/controls on bottom (login screen already uses dual-screen)
- [ ] **Scrollable output** — scroll through content longer than one screen
- [ ] **Loading screen** — progress bar during network requests
- [ ] **In-app QR scanner** — camera-based QR scanning is blocked by a hard-to-reproduce data abort during capture; deferred. Current workaround: `sdmc:/3ds/notApro/user.json` (see README)
- [ ] **Status bar** — persistent connection/error status
- [ ] **Settings menu** — language, timeout, cache duration

## 🌍 Misc

- [ ] **French/English UI strings** — i18n support; auto-detect system language at first launch (see `devkitpro/3ds-examples/get_system_language`)
- [x] **File-based logging** — write debug log to SD card (`sdmc:/3ds/notApro/log.txt`)
- [ ] **Error recovery** — user-friendly messages for network/auth failures
- [ ] **CIA packaging** — proper metadata + banner for installable CIA
