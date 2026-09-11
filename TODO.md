# TODO — notApro

Planned features, roughly in implementation order.

## 🔐 Authentication

- [ ] **PIN decryption of jeton** — decrypt the jeton from the QR code using the 4-digit PIN (AES or XOR depending on Pronote's protocol)
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

- [ ] **Dual-screen layout** — login/nav on bottom screen, content on top
- [ ] **Scrollable output** — scroll through content longer than one screen
- [ ] **Loading screen** — progress bar during network requests
- [ ] **Status bar** — persistent connection/error status
- [ ] **Settings menu** — language, timeout, cache duration

## 🌍 Misc

- [ ] **French/English UI strings** — i18n support
- [ ] **File-based logging** — write debug log to SD card (`pronote_log.txt`)
- [ ] **Error recovery** — user-friendly messages for network/auth failures
- [ ] **CIA packaging** — proper metadata + banner for installable CIA
