#include <string.h>
#include "kbc/api/http.h"
#include "kbc/auth/auth_service.h"
#include "kbc/game/game_service.h"

static const char *bearer_token(const char *authorization) {
    if (!authorization || strncmp(authorization, "Bearer ", 7)) return NULL;
    return authorization + 7;
}

static int require_user(socket_t client, const char *authorization) {
    const char *token = bearer_token(authorization);
    int user_id = token ? auth_user_id(token) : 0;
    if (!user_id) http_respond(client, 401, "Unauthorized", "{\"error\":\"Sign in to continue\"}");
    return user_id;
}

void http_route(socket_t client, const char *method, const char *path, const char *body, const char *authorization) {
    if (!strcmp(method, "OPTIONS")) {
        http_respond(client, 204, "No Content", "");
    } else if (!strcmp(method, "GET") && !strcmp(path, "/api/health")) {
        http_respond(client, 200, "OK", "{\"status\":\"ok\",\"service\":\"kbc-ai-backend\",\"message\":\"C backend is running\"}");
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/auth/register")) {
        if (!body) http_respond(client, 400, "Bad Request", "{\"error\":\"Missing HTTP request body\"}");
        else auth_register(client, body);
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/auth/login")) {
        if (!body) http_respond(client, 400, "Bad Request", "{\"error\":\"Missing HTTP request body\"}");
        else auth_login(client, body);
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/auth/guest")) {
        auth_guest(client);
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/auth/password-reset")) {
        if (!body) http_respond(client, 400, "Bad Request", "{\"error\":\"Missing HTTP request body\"}");
        else auth_reset_password(client, body);
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/auth/recovery-code")) {
        int user_id = require_user(client, authorization);
        if (user_id) auth_recovery_code(client, user_id);
    } else if (!strcmp(method, "GET") && !strcmp(path, "/api/auth/profile-picture")) {
        int user_id = require_user(client, authorization);
        if (user_id) auth_profile_picture(client, NULL, user_id, 0);
    } else if (!strcmp(method, "PUT") && !strcmp(path, "/api/auth/profile-picture")) {
        if (!body) http_respond(client, 400, "Bad Request", "{\"error\":\"Missing HTTP request body\"}");
        else {
            int user_id = require_user(client, authorization);
            if (user_id) auth_profile_picture(client, body, user_id, 1);
        }
    } else if (!strcmp(method, "GET") && !strcmp(path, "/api/auth/me")) {
        const char *token = bearer_token(authorization);
        if (!token) http_respond(client, 401, "Unauthorized", "{\"error\":\"Sign in to continue\"}");
        else auth_current_user(client, token);
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/auth/logout")) {
        const char *token = bearer_token(authorization);
        if (!token) http_respond(client, 401, "Unauthorized", "{\"error\":\"Sign in to continue\"}");
        else auth_logout(client, token);
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/game/start")) {
        if (!body) http_respond(client, 400, "Bad Request", "{\"error\":\"Missing HTTP request body\"}");
        else {
            int user_id = require_user(client, authorization);
            if (user_id) game_start(client, body, user_id);
        }
    } else if (!strcmp(method, "GET") && !strncmp(path, "/api/game/question?", 19)) {
        int user_id = require_user(client, authorization);
        if (user_id) game_get_question(client, path, user_id);
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/game/answer")) {
        if (!body) http_respond(client, 400, "Bad Request", "{\"error\":\"Missing HTTP request body\"}");
        else {
            int user_id = require_user(client, authorization);
            if (user_id) game_submit_answer(client, body, user_id);
        }
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/game/lifeline")) {
        if (!body) http_respond(client, 400, "Bad Request", "{\"error\":\"Missing HTTP request body\"}");
        else {
            int user_id = require_user(client, authorization);
            if (user_id) game_use_lifeline(client, body, user_id);
        }
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/game/finish")) {
        if (!body) http_respond(client, 400, "Bad Request", "{\"error\":\"Missing HTTP request body\"}");
        else {
            int user_id = require_user(client, authorization);
            if (user_id) game_finish(client, body, user_id);
        }
    } else if (!strcmp(method, "GET") && !strcmp(path, "/api/game/history")) {
        int user_id = require_user(client, authorization);
        if (user_id) game_history(client, user_id);
    } else if (!strcmp(path, "/") || !strcmp(path, "/api")) {
        http_respond(client, 200, "OK", "{\"service\":\"KBC AI Quiz API\",\"health_endpoint\":\"/api/health\",\"register_endpoint\":\"POST /api/auth/register\",\"login_endpoint\":\"POST /api/auth/login\",\"guest_endpoint\":\"POST /api/auth/guest\",\"password_reset_endpoint\":\"POST /api/auth/password-reset\",\"recovery_code_endpoint\":\"POST /api/auth/recovery-code (authenticated)\",\"profile_picture_endpoint\":\"GET/PUT /api/auth/profile-picture (authenticated)\",\"start_game_endpoint\":\"POST /api/game/start (authenticated)\",\"question_endpoint\":\"GET /api/game/question?gameId=...&questionNumber=...\",\"answer_endpoint\":\"POST /api/game/answer\",\"history_endpoint\":\"GET /api/game/history (authenticated)\",\"finish_endpoint\":\"POST /api/game/finish\"}");
    } else {
        http_respond(client, 404, "Not Found", "{\"error\":\"Route not found\",\"available_routes\":[\"GET /api/health\",\"POST /api/auth/register\",\"POST /api/auth/login\",\"POST /api/auth/guest\",\"POST /api/auth/password-reset\",\"POST /api/auth/recovery-code\",\"GET /api/auth/profile-picture\",\"PUT /api/auth/profile-picture\",\"GET /api/auth/me\",\"POST /api/auth/logout\",\"POST /api/game/start\",\"GET /api/game/question\",\"POST /api/game/answer\",\"POST /api/game/finish\",\"GET /api/game/history\"]}");
    }
}
