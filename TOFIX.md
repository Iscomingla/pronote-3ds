# TOFIX

## [FEATURE] HTTP login not implemented

**Branch:** `feat/network-login`

`do_login()` in `main.c` decrypts login+jeton correctly but then hits a stub.
`network.c` needs a real implementation of the Pronote 3-step challenge-response login.
See protocol details in ADVANCED.md.
