#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "kbc/api/http.h"
#ifndef _WIN32
#include <sys/time.h>
#endif

#define REQUEST_SIZE 32768

static int parse_content_length(const char *headers, size_t *content_length) {
    const char *line = strstr(headers, "\r\n");
    if (!line) return 0;
    line += 2;
    while (*line && !(line[0] == '\r' && line[1] == '\n')) {
        const char *end = strstr(line, "\r\n");
        if (!end) return 0;
        const char *colon = memchr(line, ':', (size_t)(end - line));
        if (colon && (size_t)(colon - line) == 14) {
            const char *name = "content-length";
            size_t index = 0;
            while (index < 14 && tolower((unsigned char)line[index]) == name[index]) index++;
            if (index == 14) {
                char *value_end = NULL;
                unsigned long parsed = strtoul(colon + 1, &value_end, 10);
                while (value_end < end && isspace((unsigned char)*value_end)) value_end++;
                if (value_end != end || parsed >= REQUEST_SIZE) return -1;
                *content_length = (size_t)parsed;
                return 1;
            }
        }
        line = end + 2;
    }
    *content_length = 0;
    return 1;
}

static int request_authorization(const char *request, char *output, size_t capacity) {
    const char *line = strstr(request, "\r\n");
    if (!line) return 0;
    line += 2;
    while (*line && !(line[0] == '\r' && line[1] == '\n')) {
        const char *end = strstr(line, "\r\n");
        if (!end) return 0;
        const char *colon = memchr(line, ':', (size_t)(end - line));
        if (colon && (size_t)(colon - line) == 13) {
            const char *name = "authorization";
            size_t index = 0;
            while (index < 13 && tolower((unsigned char)line[index]) == name[index]) index++;
            if (index == 13) {
                const char *value = colon + 1;
                while (value < end && isspace((unsigned char)*value)) value++;
                size_t length = (size_t)(end - value);
                while (length && isspace((unsigned char)value[length - 1])) length--;
                if (!length || length >= capacity) return 0;
                memcpy(output, value, length);
                output[length] = '\0';
                return 1;
            }
        }
        line = end + 2;
    }
    return 0;
}

void http_handle_client(socket_t client) {
#ifdef _WIN32
    DWORD timeout = 5000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout));
#else
    struct timeval timeout = {5, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
    char request[REQUEST_SIZE];
    size_t received = 0;
    size_t header_length = 0;
    while (received < sizeof(request) - 1) {
#ifdef _WIN32
        int count = recv(client, request + received, (int)(sizeof(request) - received - 1), 0);
#else
        ssize_t count = recv(client, request + received, sizeof(request) - received - 1, 0);
#endif
        if (count <= 0) break;
        received += (size_t)count;
        request[received] = '\0';
        char *separator = strstr(request, "\r\n\r\n");
        if (separator) {
            header_length = (size_t)(separator + 4 - request);
            break;
        }
    }
    if (!header_length) {
        http_respond(client, 400, "Bad Request", "{\"error\":\"Incomplete HTTP headers\"}");
        CLOSE_SOCKET(client);
        return;
    }
    size_t content_length = 0;
    int length_result = parse_content_length(request, &content_length);
    if (length_result < 0 || content_length > sizeof(request) - header_length - 1) {
        http_respond(client, 413, "Payload Too Large", "{\"error\":\"Request body is too large\"}");
        CLOSE_SOCKET(client);
        return;
    }
    while (received < header_length + content_length) {
#ifdef _WIN32
        int count = recv(client, request + received, (int)(sizeof(request) - received - 1), 0);
#else
        ssize_t count = recv(client, request + received, sizeof(request) - received - 1, 0);
#endif
        if (count <= 0) break;
        received += (size_t)count;
    }
    if (received < header_length + content_length) {
        http_respond(client, 400, "Bad Request", "{\"error\":\"Incomplete HTTP request body\"}");
        CLOSE_SOCKET(client);
        return;
    }
    request[received] = '\0';
    char method[16] = {0}, path[512] = {0}, authorization[128] = {0};
    if (sscanf(request, "%15s %511s", method, path) != 2) {
        http_respond(client, 400, "Bad Request", "{\"error\":\"Malformed HTTP request\"}");
    } else {
        request_authorization(request, authorization, sizeof(authorization));
        http_route(client, method, path, request + header_length, authorization);
    }
    CLOSE_SOCKET(client);
}
