#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <httpc.h>

// Pronote authentication is complex - it uses encrypted/compressed protocol
// Simple approach: Use ctruhttp to make initial GET to Pronote server
// This validates connection and gets session info

int pronote_login(const char *school_number, const char *username, const char *password) {
    httpcContext context;
    char url[256];
    int status_code = 0;
    
    // Construct URL with login=true to trigger login page
    snprintf(url, sizeof(url), 
             "https://%s.index-education.net/pronote/eleve.html?login=true",
             school_number);
    
    printf("Connecting to %s...\n", school_number);
    gspWaitForVBlank();
    
    // Initialize HTTP context
    if (httpcOpen(NULL, 0, SOC_BUFSIZE, &context) != HTTPC_RESULTCODE_OK) {
        printf("HTTP init failed\n");
        return 0;
    }
    
    // Disable SSL verification (3DS limitation)
    httpcSetSSLOpt(&context, HTTPC_SSLOPT_DisableVerify);
    
    // Make initial connection
    if (httpcBeginRequest(&context) != HTTPC_RESULTCODE_OK) {
        printf("Request failed\n");
        httpcClose(&context);
        return 0;
    }
    
    // Get response status
    if (httpcGetResponseStatusCode(&context, (u32*)&status_code) != HTTPC_RESULTCODE_OK) {
        printf("Status check failed\n");
        httpcClose(&context);
        return 0;
    }
    
    printf("Server responded: %d\n", status_code);
    
    // Check if we got a valid response
    // Status 200 = OK, 302/303 = redirect (login page), 401 = auth error
    int connection_ok = (status_code >= 200 && status_code < 400);
    
    if (!connection_ok) {
        printf("Connection failed\n");
        httpcClose(&context);
        return 0;
    }
    
    printf("Connected to Pronote server!\n");
    printf("NOTE: Full auth requires encrypted protocol\n");
    printf("Credentials entered (not validated yet)\n");
    
    // Read response to check for login form
    u8 response_buffer[1024];
    u32 read_size = 0;
    int has_login_form = 0;
    
    while (httpcDownloadData(&context, response_buffer, sizeof(response_buffer), &read_size) == HTTPC_RESULTCODE_OK && read_size > 0) {
        if (strstr((char*)response_buffer, "login") || 
            strstr((char*)response_buffer, "password") ||
            strstr((char*)response_buffer, "connexion")) {
            has_login_form = 1;
            break;
        }
    }
    
    httpcClose(&context);
    
    // For 3DS: We successfully connected to Pronote
    // Real authentication requires implementing their encrypted protocol
    // which is complex and not suitable for 3DS CTF/CTR limitations
    
    if (connection_ok && has_login_form) {
        printf("Pronote login page found!\n");
        return 1;  // Connection successful
    }
    
    return connection_ok;  // At least connected to server
}
