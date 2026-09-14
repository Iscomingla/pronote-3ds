#ifndef NETWORK_H
#define NETWORK_H

#include <stddef.h>

/*
 * pronote_login — full Pronote QR-code login flow.
 *
 * Parameters:
 *   url          — school URL from QR JSON, e.g.
 *                  "https://0260008t.index-education.net/pronote/mobile.eleve.html"
 *   username     — plaintext username (decrypted from QR 'login' field)
 *   password     — plaintext password (decrypted from QR 'jeton' field)
 *   uuid         — stable device UUID (must not change between logins)
 *   out_token    — on success, receives the new jetonConnexionAppliMobile
 *                  (use this as password for the next login)
 *   out_token_sz — size of out_token buffer
 *
 * Returns  0 on success, negative on error.
 * Error codes:
 *   -1  httpc init / connection failed
 *   -2  HTTP request failed
 *   -3  could not parse session HTML (onload attr)
 *   -4  FonctionParametres request failed
 *   -5  Identification request failed
 *   -6  challenge solve failed
 *   -7  Authentification request failed
 *   -8  missing jetonConnexionAppliMobile in response
 */
int pronote_login(const char *url,
                  const char *username,
                  const char *password,
                  const char *uuid,
                  char       *out_token,
                  size_t      out_token_sz);

#endif /* NETWORK_H */
