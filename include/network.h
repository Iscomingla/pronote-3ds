#ifndef NETWORK_H
#define NETWORK_H

// Attempt login to Pronote
// Returns: 1 if successful, 0 if failed
int pronote_login(const char *school_number, const char *username, const char *password);

#endif
