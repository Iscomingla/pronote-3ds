# 3DS Pronote App - Quick Start Guide

## TL;DR: Get it running in 5 minutes

### Prerequisites
- ✅ devkitPro installed with devkitARM
- ✅ `pacman -S 3ds-curl 3ds-mbedtls 3ds-zlib`
- ✅ `export DEVKITARM=/path/to/devkitARM`

### Build

```bash
# 1. Create project
mkdir pronote-3ds && cd pronote-3ds
mkdir src

# 2. Copy files
cp ../pronote_3ds_enhanced.c src/main.c
cp ../Makefile .

# 3. Build
make clean
make

# 4. Result: pronote-3ds.3dsx (copy to 3DS!)
```

### Install on 3DS

```
SD Card: /3ds/pronote-3ds.3dsx
Launch from Homebrew Channel
```

### Use It

| Key | Action |
|-----|--------|
| **↑↓** | Switch field |
| **A** | Type text |
| **B** | Delete char |
| **Y** | Clear field |
| **X** | Login |
| **START** | Exit |

---

## Project Files

### Source Code (Pick One)
- **pronote_3ds_enhanced.c** ← Use this (has software keyboard)
- **pronote_3ds.c** - Simpler version

### Build Configuration
- **Makefile** - Handles compilation for 3DS

### Documentation
- **README.md** - Full guide with troubleshooting
- **STRUCTURE.md** - Directory layout and architecture
- **QUICK_START.md** - This file

---

## What It Does

```
Login Screen
    ↓
Enter school code (e.g., "0260008t")
    ↓
Enter username & password
    ↓
Press X to login
    ↓
Connects to: https://{school}.index-education.net/pronote/eleve.html
    ↓
Shows success/failure
```

---

## Common Issues

### Build fails: "DEVKITARM not set"
```bash
export DEVKITARM=/opt/devkitpro/devkitARM
# Then rebuild: make clean && make
```

### Build fails: "libctru not found"
```bash
pacman -S ppc-libctru 3ds-libctru
```

### App crashes on 3DS
- Check 3DS homebrew support (HBL/CFW installed?)
- Try rebuilding
- Check WiFi is working

### "Login failed" on app
- Verify school code (example: 0260008t)
- Check username/password
- School server may be down

---

## File Sizes

| File | Size | Purpose |
|------|------|---------|
| pronote_3ds.3dsx | ~200KB | What you copy to 3DS |
| pronote_3ds.elf | ~500KB | Debug/reference |
| Source code | ~10KB | Edit and rebuild |

---

## How It Works (Technical)

1. **3DS Input** → User enters school code, username, password
2. **libctru Graphics** → Renders simple text UI
3. **libctru Input** → Reads buttons and keyboard
4. **curl Library** → Makes HTTPS POST request to Pronote
5. **Response Parse** → Checks for login indicators in HTML
6. **Display Result** → Shows success or error message

### HTTP Request
```
POST https://{school}.index-education.net/pronote/eleve.html
Content-Type: application/x-www-form-urlencoded

login=username&password=password&urlRetour=
```

---

## Customization Quick Tips

### Change button mapping
Edit `pronote_3ds_enhanced.c` main loop:
```c
if (kdown & KEY_X) {  // Press X to login
    perform_login(...);
}
```

### Add more fields
Edit `AppState` struct and UI functions:
```c
typedef struct {
    char field1[SIZE];
    char field2[SIZE];
    // Add your fields here
} AppState;
```

### Change keyboard text
Edit in `open_keyboard()`:
```c
swkbdSetHintText(&swkbd, "Your text here");
```

### Handle response differently
Edit `perform_login()`:
```c
if (strstr(resp.memory, "SUCCESS")) {
    // Your logic
}
```

---

## Next Steps After Building

1. **Test on 3DS** - Does it connect? Does login work?
2. **Parse Response** - Extract actual grades/info from HTML
3. **Add Display** - Show Pronote data on both screens
4. **Add Features** - Grade tracking, alerts, etc.

---

## Resources

- **devkitPro**: https://devkitpro.org
- **libctru Docs**: https://libctru.devkitpro.org
- **curl Docs**: https://curl.se/libcurl/c
- **Pronote**: https://www.index-education.net

---

## License & Disclaimer

- **License**: MIT (free to use and modify)
- **Not Official**: Not affiliated with Pronote/Index-Education
- **Your Risk**: Use responsibly and follow school policies
- **Security**: Passwords sent over HTTPS but 3DS cert verification limited

---

**Happy coding! 🎮** Feel free to fork, modify, and improve this app!
