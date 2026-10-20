#ifndef KBC_HTTP_H
#define KBC_HTTP_H

#include "kbc/platform/platform.h"

void http_respond(socket_t client, int status, const char *label, const char *body);
void http_route(socket_t client, const char *method, const char *path, const char *body, const char *authorization);
void http_handle_client(socket_t client);
int http_server_run(void);

#endif