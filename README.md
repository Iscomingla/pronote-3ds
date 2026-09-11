# 3DS Pronote Login App

A Nintendo 3DS homebrew application to access Pronote (French school management system) using your school code, username, and password.

## Requirements

- **devkitPro** with devkitARM installed
  - https://devkitpro.org/wiki/Getting_Started
- **libctru** - The 3DS C library (included with devkitPro)
- **curl** with SSL/TLS support (installed via devkitPro pacman)
- **mbedTLS** for cryptography (installed via devkitPro pacman)

## Installation

### 1. Install devkitPro

Follow the instructions at https://devkitpro.org/wiki/Getting_Started

### 2. Install Required Libraries

```bash
pacman -S 3ds-curl 3ds-mbedtls 3ds-zlib
```

### 3. Set Environment Variables

```bash
export DEVKITARM=/path/to/devkitARM  # Usually set by devkitPro installer
export PATH=${DEVKITARM}/bin:$PATH
```

### 4. Build the Application

Navigate to the project directory and run:

```bash
make clean
make
```

This will generate:
- `pronote_3ds.3dsx` - The homebrew app file (for 3DS)
- `pronote_3ds.elf` - The ELF executable

## Installation on 3DS

1. Copy `pronote_3ds.3dsx` to your 3DS SD card:
   ```
   /3ds/pronote_3ds.3dsx
   ```

2. Launch using Homebrew Channel or similar launcher

## Usage

### Controls

| Button | Action |
|--------|--------|
| **UP/DOWN** | Navigate between fields (School Code, Username, Password) |
| **A** | Open on-screen keyboard to input text |
| **B** | Delete last character in current field |
| **Y** | Clear entire field |
| **X** | Attempt login |
| **START** | Exit application |

### Login Flow

1. **Enter School Code**
   - Example: `0260008t`
   - Navigate to "School Code" field
   - Press A to open keyboard
   - Type your school code and confirm

2. **Enter Username**
   - Navigate to "Username" field
   - Press A and enter your Pronote username

3. **Enter Password**
   - Navigate to "Password" field
   - Press A and enter your password (displayed as asterisks)

4. **Login**
   - Press X to attempt connection
   - Status message will indicate success or failure
   - App will attempt to fetch your Pronote dashboard

## Files

### Main Application
- **pronote_3ds_enhanced.c** - Enhanced version with software keyboard support (RECOMMENDED)
- **pronote_3ds.c** - Basic version with manual input

### Build Files
- **Makefile** - Build configuration for devkitPro

## Features

✅ HTTPS connection to Pronote servers  
✅ Secure password input (masked display)  
✅ Software keyboard for easy text input  
✅ Persistent status messages  
✅ Connection validation  

## Technical Details

### Architecture
- Uses **libctru** for 3DS graphics and input
- Uses **curl** with **mbedTLS** for secure HTTP(S) connections
- Implements POST-based authentication to Pronote API

### Security Notes
- Passwords are sent over HTTPS (SSL/TLS verified disabled due to 3DS cert limitations)
- Consider your 3DS network security when entering credentials
- Do NOT use this on untrusted networks

### Network Requirements
- 3DS connected to WiFi
- Network configured for WiFi access (via 3DS System Settings)

## Troubleshooting

### "Failed to init CURL"
- Ensure curl libraries are properly installed: `pacman -S 3ds-curl`
- Rebuild: `make clean && make`

### "Connection error"
- Check 3DS WiFi connection in System Settings
- Verify school code format (usually lowercase alphanumeric)
- School server may be down or unreachable

### "Login failed"
- Verify username and password are correct
- Check school code is accurate
- Pronote server may have different authentication requirements

### App crashes on launch
- Ensure your 3DS has proper homebrew support
- Try launching from Homebrew Channel
- Rebuild and reinstall the .3dsx file

## Development

### Modifying the Application

The main login logic is in `perform_login()`:
```c
int perform_login(const char *school_number, const char *username, const char *password)
```

To modify:
1. Edit the `.c` file
2. Run `make clean && make`
3. Copy new `.3dsx` to 3DS SD card

### Future Enhancements
- Display Pronote dashboard on 3DS screen
- Parse grades and assignments
- Show calendar events
- Save credentials securely (with user consent)
- Multi-language support

## Limitations

- 3DS screen size (320x240) limits detailed Pronote display
- No touchscreen HTML rendering
- Current version only attempts login, doesn't display dashboard
- Pronote server may have anti-bot measures

## License

MIT License - Feel free to modify and distribute

## Disclaimer

This application is a homebrew project and is NOT affiliated with Index-Education or Pronote.  
Use at your own risk. The developer is not responsible for:
- Account security issues
- Pronote server bans or restrictions
- Data loss or privacy concerns
- Violation of school policies

Always follow your school's policies regarding Pronote access.

## Resources

- **devkitPro Documentation**: https://devkitpro.org/wiki/
- **libctru Reference**: https://libctru.devkitpro.org/
- **Pronote**: https://www.index-education.net/
- **curl Documentation**: https://curl.se/libcurl/c/

## Support

If you encounter issues:
1. Check the troubleshooting section above
2. Verify all dependencies are installed correctly
3. Try rebuilding from scratch: `make clean && make`
4. Check devkitPro documentation for 3DS-specific issues
