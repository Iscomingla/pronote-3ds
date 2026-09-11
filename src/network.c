#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <httpc.h>

// Simple HTTP POST to Pronote
// Returns: 1 if login successful, 0 if failed

int pronote_login(const char *school_number, const char *username, const char *password) {
    httpcContext context;
    char url[256];
    char post_data[256];
    int status_code = 0;
    
    // Construct URL
    snprintf(url, sizeof(url), 
             "https://%s.index-education.net/pronote/eleve.html",
             school_number);
    
    // Construct POST data
    snprintf(post_data, sizeof(post_data),
             "login=%s&password=%s&urlRetour=",
             username, password);
    
    printf("Connecting to %s...\n", school_number);
    
    // Initialize HTTP context
    if (httpcOpen(NULL, 0, SOC_BUFSIZE, &context) != HTTPC_RESULTCODE_OK) {
        printf("Failed to init HTTP\n");
        return 0;
    }
    
    // Add request header
    httpcSetSSLOpt(&context, HTTPC_SSLOPT_DisableVerify);  // Disable SSL verification for 3DS
    httpcAddRequestHeaderField(&context, "Content-Type", "application/x-www-form-urlencoded");
    
    // Begin request
    if (httpcBeginRequest(&context) != HTTPC_RESULTCODE_OK) {
        printf("Failed to begin request\n");
        httpcClose(&context);
        return 0;
    }
    
    // Post data
    if (httpcPostData(&context, (u8*)post_data, strlen(post_data)) != HTTPC_RESULTCODE_OK) {
        printf("Failed to post data\n");
        httpcClose(&context);
        return 0;
    }
    
    // Get response status
    if (httpcGetResponseStatusCode(&context, (u32*)&status_code) != HTTPC_RESULTCODE_OK) {
        printf("Failed to get status\n");
        httpcClose(&context);
        return 0;
    }
    
    printf("Status code: %d\n", status_code);
    
    // Read response (check for success indicators)
    u8 response_buffer[1024];
    u32 read_size = 0;
    int login_success = 0;
    
    while (httpcDownloadData(&context, response_buffer, sizeof(response_buffer), &read_size) == HTTPC_RESULTCODE_OK && read_size > 0) {
        // Check for login success indicators
        if (strstr((char*)response_buffer, "logout") || 
            strstr((char*)response_buffer, "deconnexion") ||
            strstr((char*)response_buffer, "AccueilAncienLoginPage")) {
            login_success = 1;
            break;
        }
    }
    
    // Cleanup
    httpcClose(&context);
    
    return login_success;
}
