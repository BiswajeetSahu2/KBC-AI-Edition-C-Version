#ifndef KBC_GAME_SERVICE_H
#define KBC_GAME_SERVICE_H

#include "kbc/platform/platform.h"

void game_start(socket_t client, const char *body, int user_id);
void game_get_question(socket_t client, const char *path, int user_id);
void game_submit_answer(socket_t client, const char *body, int user_id);
void game_finish(socket_t client, const char *body, int user_id);
void game_use_lifeline(socket_t client, const char *body, int user_id);
void game_history(socket_t client, int user_id);

#endif
