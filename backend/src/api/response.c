#include <stdio.h>
#include <string.h>
#include "kbc/api/http.h"

void http_respond(socket_t client, int status, const char *label, const char *body) {
    char output[32768];
    int length = snprintf(output, sizeof(output),
        "HTTP/1.1 %d %s\r\nContent-Type: application/json; charset=utf-8\r\n"
        "Content-Length: %u\r\nAccess-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        "Connection: close\r\n\r\n%s",
        status, label, (unsigned)strlen(body), body);
    if (length <= 0 || length >= (int)sizeof(output)) return;
    int sent = 0;
    while (sent < length) {
#ifdef _WIN32
        int count = send(client, output + sent, length - sent, 0);
#else
        ssize_t count = send(client, output + sent, (size_t)(length - sent), 0);
#endif
        if (count <= 0) break;
        sent += (int)count;
    }
}
