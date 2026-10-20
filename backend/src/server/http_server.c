#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kbc/api/http.h"

#define DEFAULT_PORT 8084

int http_server_run(void) {
    int port = DEFAULT_PORT;
    const char *port_setting = getenv("KBC_PORT");
    if (port_setting && *port_setting) {
        char *end = NULL;
        long parsed = strtol(port_setting, &end, 10);
        if (end && !*end && parsed > 0 && parsed <= 65535) port = (int)parsed;
        else fprintf(stderr, "Invalid KBC_PORT; using default port %d.\n", DEFAULT_PORT);
    }
    socket_t server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == BAD_SOCKET) {
        fprintf(stderr, "Could not create server socket.\n");
        return 1;
    }
#ifndef _WIN32
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((unsigned short)port);
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) != 0) {
        fprintf(stderr, "Could not bind port %d. Stop the previous server first.\n", port);
        CLOSE_SOCKET(server);
        return 1;
    }
    if (listen(server, 10) != 0) {
        fprintf(stderr, "Could not listen on port %d.\n", port);
        CLOSE_SOCKET(server);
        return 1;
    }
    printf("KBC AI C backend running at http://localhost:%d\n", port);
    printf("Endpoints: GET /api/health | POST /api/game/start | GET /api/game/question | POST /api/game/answer | POST /api/game/finish\n");
    printf("Press Ctrl+C to stop.\n");
    for (;;) {
        struct sockaddr_in client_address;
#ifdef _WIN32
        int length = sizeof(client_address);
#else
        socklen_t length = sizeof(client_address);
#endif
        socket_t client = accept(server, (struct sockaddr *)&client_address, &length);
        if (client != BAD_SOCKET) http_handle_client(client);
    }
    CLOSE_SOCKET(server);
    return 0;
}
