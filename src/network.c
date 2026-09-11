#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network.h"

// Stub implementation - full HTTP support coming later
// For now, just validates connection locally

int pronote_login(const char *school_number, const char *username, const char *password) {
    printf("Login stub: %s@%s\n", username, school_number);
    return 0;  // Not implemented yet
}
