#ifndef KBC_AUTH_SERVICE_H
#define KBC_AUTH_SERVICE_H

#include "kbc/platform/platform.h"

void auth_register(socket_t client, const char *body);
void auth_login(socket_t client, const char *body);
void auth_guest(socket_t client);
void auth_current_user(socket_t client, const char *token);
void auth_logout(socket_t client, const char *token);
void auth_profile_picture(socket_t client, const char *body, int user_id, int save);
void auth_recovery_code(socket_t client, int user_id);
void auth_reset_password(socket_t client, const char *body);
int auth_user_id(const char *token);

#endif
