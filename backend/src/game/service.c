#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kbc/storage/database.h"
#include "kbc/game/game_service.h"
#include "kbc/api/http.h"
#include "kbc/api/request_parser.h"
#include "kbc/game/question_bank.h"

typedef struct {
    long id;
    int total;
    int question_number;
    int correct_answers;
    char player[64];
    char mode[24];
    char status[16];
    char subjects[5][64];
} GameRecord;

typedef struct {
    long id;
    char subject[64];
    char question[768];
    char options[4][256];
    char answer[2];
    char explanation[768];
} QuestionRecord;

static const char answer_letters[] = "ABCD";

static int parse_game_id(const char *value, long *id) {
    if (strncmp(value, "game-", 5)) return 0;
    char *end = NULL;
    long parsed = strtol(value + 5, &end, 10);
    if (!end || *end || parsed < 1) return 0;
    *id = parsed;
    return 1;
}

static int load_game(long id, int user_id, GameRecord *game) {
    sqlite3 *database = database_connection();
    sqlite3_stmt *statement = NULL;
    const char *sql = "SELECT player_name, mode, total_questions, question_number, correct_answers, status "
        "FROM games WHERE id = ? AND user_id = ?";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(statement, 1, id);
    sqlite3_bind_int(statement, 2, user_id);
    if (sqlite3_step(statement) != SQLITE_ROW) {
        sqlite3_finalize(statement);
        return 0;
    }
    memset(game, 0, sizeof(*game));
    game->id = id;
    snprintf(game->player, sizeof(game->player), "%s", sqlite3_column_text(statement, 0));
    snprintf(game->mode, sizeof(game->mode), "%s", sqlite3_column_text(statement, 1));
    game->total = sqlite3_column_int(statement, 2);
    game->question_number = sqlite3_column_int(statement, 3);
    game->correct_answers = sqlite3_column_int(statement, 4);
    snprintf(game->status, sizeof(game->status), "%s", sqlite3_column_text(statement, 5));
    sqlite3_finalize(statement);

    if (sqlite3_prepare_v2(database, "SELECT subject FROM game_subjects WHERE game_id = ? ORDER BY position", -1, &statement, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(statement, 1, id);
    int count = 0;
    while (count < 5 && sqlite3_step(statement) == SQLITE_ROW) {
        snprintf(game->subjects[count], sizeof(game->subjects[count]), "%s", sqlite3_column_text(statement, 0));
        count++;
    }
    sqlite3_finalize(statement);
    return count == 5;
}

static int assign_question(sqlite3 *database, long game_id, int number, const char *subject) {
    sqlite3_stmt *statement = NULL;
    const char *select_sql =
        "SELECT q.id FROM questions q WHERE q.subject = ? "
        "ORDER BY (SELECT COUNT(*) FROM game_questions current "
        "WHERE current.game_id = ? AND current.question_id = q.id), "
        "(SELECT COUNT(*) FROM game_questions recent "
        "JOIN games recent_game ON recent_game.id = recent.game_id "
        "WHERE recent.question_id = q.id AND recent.game_id != ? "
        "AND recent_game.created_at >= datetime('now', '-30 days')), random() LIMIT 1";
    if (sqlite3_prepare_v2(database, select_sql, -1, &statement, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(statement, 1, subject, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 2, game_id);
    sqlite3_bind_int64(statement, 3, game_id);
    if (sqlite3_step(statement) != SQLITE_ROW) {
        sqlite3_finalize(statement);
        return 0;
    }
    sqlite3_int64 question_id = sqlite3_column_int64(statement, 0);
    sqlite3_finalize(statement);

    const char *insert_sql = "INSERT INTO game_questions (game_id, question_number, question_id) VALUES (?, ?, ?)";
    if (sqlite3_prepare_v2(database, insert_sql, -1, &statement, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(statement, 1, game_id);
    sqlite3_bind_int(statement, 2, number);
    sqlite3_bind_int64(statement, 3, question_id);
    int success = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return success;
}

static int create_question_plan(sqlite3 *database, long game_id, char subjects[5][64], int total) {
    for (int number = 1; number <= total; number++) {
        const char *subject = subjects[(number - 1) % 5];
        if (!assign_question(database, game_id, number, subject)) return 0;
    }
    return 1;
}

static int extend_endless_plan(sqlite3 *database, long game_id, char subjects[5][64], int start_number) {
    int end_number = start_number + 49;
    if (end_number > 10000) end_number = 10000;
    for (int number = start_number; number <= end_number; number++) {
        const char *subject = subjects[(number - 1) % 5];
        if (!assign_question(database, game_id, number, subject)) return 0;
    }
    return 1;
}

static int load_question(long game_id, int number, QuestionRecord *question) {
    sqlite3 *database = database_connection();
    sqlite3_stmt *statement = NULL;
    const char *sql = "SELECT id, subject, question, option_a, option_b, option_c, option_d, answer, explanation "
        "FROM questions JOIN game_questions ON questions.id = game_questions.question_id "
        "WHERE game_questions.game_id = ? AND game_questions.question_number = ?";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(statement, 1, game_id);
    sqlite3_bind_int(statement, 2, number);
    if (sqlite3_step(statement) != SQLITE_ROW) {
        sqlite3_finalize(statement);
        return 0;
    }
    question->id = sqlite3_column_int64(statement, 0);
    snprintf(question->subject, sizeof(question->subject), "%s", sqlite3_column_text(statement, 1));
    snprintf(question->question, sizeof(question->question), "%s", sqlite3_column_text(statement, 2));
    for (int index = 0; index < 4; index++) {
        snprintf(question->options[index], sizeof(question->options[index]), "%s", sqlite3_column_text(statement, index + 3));
    }
    snprintf(question->answer, sizeof(question->answer), "%s", sqlite3_column_text(statement, 7));
    snprintf(question->explanation, sizeof(question->explanation), "%s", sqlite3_column_text(statement, 8));
    sqlite3_finalize(statement);
    return 1;
}

static int serialize_question(long game_id, int number, int total, const QuestionRecord *question,
    char *response, size_t capacity) {
    char subject[160], text[1536], options[4][512];
    if (!json_escape(question->subject, subject, sizeof(subject)) ||
        !json_escape(question->question, text, sizeof(text))) return 0;
    for (int index = 0; index < 4; index++) {
        if (!json_escape(question->options[index], options[index], sizeof(options[index]))) return 0;
    }
    const char *difficulty = number <= 4 ? "Easy" : number <= 8 ? "Moderate" : number <= 12 ? "Hard" : "Expert";
    int length = snprintf(response, capacity,
        "{\"gameId\":\"game-%ld\",\"questionNumber\":%d,\"totalQuestions\":%d,\"subject\":\"%s\",\"difficulty\":\"%s\",\"question\":\"%s\",\"options\":{\"A\":\"%s\",\"B\":\"%s\",\"C\":\"%s\",\"D\":\"%s\"}}",
        game_id, number, total, subject, difficulty, text, options[0], options[1], options[2], options[3]);
    return length > 0 && (size_t)length < capacity;
}

static sqlite3_int64 flip_question_id(sqlite3 *database, long game_id, int question_number,
    const char *subject, sqlite3_int64 old_question_id) {
    sqlite3_stmt *statement = NULL;
    const char *sql =
        "SELECT q.id FROM questions q WHERE q.subject = ? AND q.id != ? "
        "AND NOT EXISTS (SELECT 1 FROM game_questions prior "
        "WHERE prior.game_id = ? AND prior.question_id = q.id) "
        "ORDER BY (SELECT COUNT(*) FROM game_questions recent "
        "JOIN games recent_game ON recent_game.id = recent.game_id "
        "WHERE recent.question_id = q.id AND recent.game_id != ? "
        "AND recent_game.created_at >= datetime('now', '-30 days')), random() LIMIT 1";
    (void)question_number;
    if (sqlite3_prepare_v2(database, sql, -1, &statement, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(statement, 1, subject, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 2, old_question_id);
    sqlite3_bind_int64(statement, 3, game_id);
    sqlite3_bind_int64(statement, 4, game_id);
    sqlite3_int64 selected = sqlite3_step(statement) == SQLITE_ROW ? sqlite3_column_int64(statement, 0) : 0;
    sqlite3_finalize(statement);
    return selected;
}

static int game_identifier(const char *body, long *id) {
    char value[32];
    return json_string(body, "gameId", value, sizeof(value)) && parse_game_id(value, id);
}

void game_start(socket_t client, const char *body, int user_id) {
    char player[64] = {0}, mode[24] = {0}, subjects[5][64] = {{0}};
    if (!json_string(body, "playerName", player, sizeof(player))) {
        http_respond(client, 400, "Bad Request", "{\"error\":\"playerName is required\"}");
        return;
    }
    if (!json_string(body, "mode", mode, sizeof(mode)) ||
        (strcmp(mode, "kbc") && strcmp(mode, "practice") && strcmp(mode, "endless"))) {
        http_respond(client, 400, "Bad Request", "{\"error\":\"mode must be kbc, practice, or endless\"}");
        return;
    }
    if (!json_subjects(body, subjects)) {
        http_respond(client, 400, "Bad Request", "{\"error\":\"Provide exactly 5 non-empty subjects\"}");
        return;
    }
    int total = !strcmp(mode, "kbc") ? 15 : !strcmp(mode, "practice") ? 50 : 0;
    sqlite3 *database = database_connection();
    sqlite3_stmt *statement = NULL;
    if (sqlite3_exec(database, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(database, "INSERT INTO games (user_id, player_name, mode, total_questions) VALUES (?, ?, ?, ?)", -1, &statement, NULL) != SQLITE_OK) {
        sqlite3_exec(database, "ROLLBACK", NULL, NULL, NULL);
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not create game\"}");
        return;
    }
    sqlite3_bind_int(statement, 1, user_id);
    sqlite3_bind_text(statement, 2, player, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, mode, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 4, total);
    int success = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    long id = sqlite3_last_insert_rowid(database);
    if (success && sqlite3_prepare_v2(database, "INSERT INTO game_subjects (game_id, position, subject) VALUES (?, ?, ?)", -1, &statement, NULL) == SQLITE_OK) {
        for (int index = 0; index < 5 && success; index++) {
            sqlite3_bind_int64(statement, 1, id);
            sqlite3_bind_int(statement, 2, index);
            sqlite3_bind_text(statement, 3, subjects[index], -1, SQLITE_TRANSIENT);
            success = sqlite3_step(statement) == SQLITE_DONE;
            sqlite3_reset(statement);
            sqlite3_clear_bindings(statement);
        }
        sqlite3_finalize(statement);
    } else success = 0;
    int planned_total = total ? total : !strcmp(mode, "endless") ? 50 : 0;
    if (success && planned_total) success = create_question_plan(database, id, subjects, planned_total);
    if (!success || sqlite3_exec(database, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_exec(database, "ROLLBACK", NULL, NULL, NULL);
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not save game\"}");
        return;
    }
    char response[512], escaped_player[160];
    json_escape(player, escaped_player, sizeof(escaped_player));
    snprintf(response, sizeof(response), "{\"status\":\"created\",\"gameId\":\"game-%ld\",\"playerName\":\"%s\",\"mode\":\"%s\",\"questionCount\":%d}", id, escaped_player, mode, total);
    http_respond(client, 201, "Created", response);
}

void game_get_question(socket_t client, const char *path, int user_id) {
    char id_text[32] = {0}, number_text[16] = {0};
    if (!query_value(path, "gameId", id_text, sizeof(id_text)) ||
        !query_value(path, "questionNumber", number_text, sizeof(number_text))) {
        http_respond(client, 400, "Bad Request", "{\"error\":\"Required query parameters: gameId and questionNumber\"}");
        return;
    }
    long id = 0;
    char *end = NULL;
    long number = strtol(number_text, &end, 10);
    GameRecord game;
    QuestionRecord question;
    if (!parse_game_id(id_text, &id) || !end || *end || number < 1 || number > 10000) {
        http_respond(client, 400, "Bad Request", "{\"error\":\"Invalid gameId or questionNumber\"}");
        return;
    }
    if (!load_game(id, user_id, &game)) {
        http_respond(client, 404, "Not Found", "{\"error\":\"Game not found\"}");
        return;
    }
    if (strcmp(game.status, "active") || number != game.question_number || (game.total && number > game.total)) {
        http_respond(client, 409, "Conflict", "{\"error\":\"This question is not the active question for the game\"}");
        return;
    }
    if (!load_question(id, (int)number, &question)) {
        int assigned = game.total
            ? 0
            : extend_endless_plan(database_connection(), id, game.subjects, (int)number);
        if (game.total || !assigned ||
            !load_question(id, (int)number, &question)) {
            http_respond(client, 404, "Not Found", "{\"error\":\"No questions are available for the selected subjects\"}");
            return;
        }
    }
    if (!question.id) {
        http_respond(client, 404, "Not Found", "{\"error\":\"No questions are available for the selected subjects\"}");
        return;
    }
    char response[4096];
    if (!serialize_question(id, (int)number, game.total, &question, response, sizeof(response))) {
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not format question\"}");
        return;
    }
    http_respond(client, 200, "OK", response);
}

void game_submit_answer(socket_t client, const char *body, int user_id) {
    long id = 0, number = 0;
    char answer[8] = {0};
    if (!game_identifier(body, &id) || !json_integer(body, "questionNumber", &number) ||
        !json_string(body, "answer", answer, sizeof(answer)) ||
        (strcmp(answer, "TIMEOUT") && (strlen(answer) != 1 || !strchr("ABCD", answer[0])))) {
        http_respond(client, 400, "Bad Request", "{\"error\":\"Provide gameId, questionNumber, and answer A-D\"}");
        return;
    }
    GameRecord game;
    QuestionRecord question;
    if (!load_game(id, user_id, &game)) {
        http_respond(client, 404, "Not Found", "{\"error\":\"Game not found\"}");
        return;
    }
    if (strcmp(game.status, "active") || number != game.question_number) {
        http_respond(client, 409, "Conflict", "{\"error\":\"This answer is not for the active question\"}");
        return;
    }
    if (!load_question(id, (int)number, &question)) {
        http_respond(client, 404, "Not Found", "{\"error\":\"Question not found\"}");
        return;
    }
    int correct = answer[0] == question.answer[0];
    int score = game.correct_answers + correct;
    int retry_available = 0;
    if (!correct && !strcmp(game.mode, "kbc")) {
        sqlite3_stmt *lifeline = NULL;
        if (sqlite3_prepare_v2(database_connection(),
            "SELECT attempts_used FROM game_lifelines WHERE game_id = ? AND lifeline = 'double_dip' AND question_number = ?",
            -1, &lifeline, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(lifeline, 1, id);
            sqlite3_bind_int(lifeline, 2, (int)number);
            if (sqlite3_step(lifeline) == SQLITE_ROW && sqlite3_column_int(lifeline, 0) == 0) retry_available = 1;
            sqlite3_finalize(lifeline);
        }
    }
    if (retry_available) {
        sqlite3_stmt *update = NULL;
        if (sqlite3_prepare_v2(database_connection(),
            "UPDATE game_lifelines SET attempts_used = 1 WHERE game_id = ? AND lifeline = 'double_dip' AND question_number = ? AND attempts_used = 0",
            -1, &update, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(update, 1, id);
            sqlite3_bind_int(update, 2, (int)number);
            retry_available = sqlite3_step(update) == SQLITE_DONE && sqlite3_changes(database_connection()) == 1;
            sqlite3_finalize(update);
        } else retry_available = 0;
    }
    if (retry_available) {
        char response[256];
        snprintf(response, sizeof(response), "{\"correct\":false,\"retryAllowed\":true,\"correctAnswers\":%d,\"gameOver\":false,\"questionNumber\":%ld,\"totalQuestions\":%d}", game.correct_answers, number, game.total);
        http_respond(client, 200, "OK", response);
        return;
    }
    int complete = (!strcmp(game.mode, "kbc") && !correct) || (game.total && number >= game.total);
    const char *status = complete ? "completed" : "active";
    sqlite3 *database = database_connection();
    sqlite3_stmt *statement = NULL;
    if (sqlite3_exec(database, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(database, "INSERT INTO game_answers (game_id, question_number, question_id, selected_answer, is_correct) VALUES (?, ?, ?, ?, ?)", -1, &statement, NULL) != SQLITE_OK) {
        sqlite3_exec(database, "ROLLBACK", NULL, NULL, NULL);
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not save answer\"}");
        return;
    }
    sqlite3_bind_int64(statement, 1, id);
    sqlite3_bind_int(statement, 2, (int)number);
    sqlite3_bind_int64(statement, 3, question.id);
    sqlite3_bind_text(statement, 4, answer, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 5, correct);
    int success = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (success && sqlite3_prepare_v2(database, "UPDATE games SET question_number = ?, correct_answers = ?, status = ? WHERE id = ? AND status = 'active'", -1, &statement, NULL) == SQLITE_OK) {
        sqlite3_bind_int(statement, 1, (int)number + 1);
        sqlite3_bind_int(statement, 2, score);
        sqlite3_bind_text(statement, 3, status, -1, SQLITE_STATIC);
        sqlite3_bind_int64(statement, 4, id);
        success = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database) == 1;
        sqlite3_finalize(statement);
    } else success = 0;
    if (!success || sqlite3_exec(database, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_exec(database, "ROLLBACK", NULL, NULL, NULL);
        http_respond(client, 409, "Conflict", "{\"error\":\"Answer was already submitted or could not be saved\"}");
        return;
    }
    char explanation[1536], response[2048];
    json_escape(question.explanation, explanation, sizeof(explanation));
    snprintf(response, sizeof(response), "{\"correct\":%s,\"correctAnswer\":\"%s\",\"explanation\":\"%s\",\"correctAnswers\":%d,\"gameOver\":%s,\"questionNumber\":%ld,\"totalQuestions\":%d}", correct ? "true" : "false", question.answer, explanation, score, complete ? "true" : "false", number, game.total);
    http_respond(client, 200, "OK", response);
}

void game_use_lifeline(socket_t client, const char *body, int user_id) {
    long id = 0;
    char lifeline[24] = {0}, subject[64] = {0};
    if (!game_identifier(body, &id) || !json_string(body, "lifeline", lifeline, sizeof(lifeline))) {
        http_respond(client, 400, "Bad Request", "{\"error\":\"Provide gameId and a valid lifeline\"}");
        return;
    }
    int fifty_fifty = !strcmp(lifeline, "fifty_fifty");
    int expert = !strcmp(lifeline, "expert");
    int flip = !strcmp(lifeline, "flip");
    int double_dip = !strcmp(lifeline, "double_dip");
    if (!fifty_fifty && !expert && !flip && !double_dip) {
        http_respond(client, 400, "Bad Request", "{\"error\":\"Unknown lifeline\"}");
        return;
    }
    if (flip && !json_string(body, "subject", subject, sizeof(subject))) {
        http_respond(client, 400, "Bad Request", "{\"error\":\"Choose a subject for the replacement question\"}");
        return;
    }

    GameRecord game;
    QuestionRecord question;
    if (!load_game(id, user_id, &game)) {
        http_respond(client, 404, "Not Found", "{\"error\":\"Game not found\"}");
        return;
    }
    if (strcmp(game.mode, "kbc") || strcmp(game.status, "active") || game.question_number >= 15) {
        http_respond(client, 409, "Conflict", "{\"error\":\"Lifelines are available only before the final KBC question\"}");
        return;
    }
    if (flip) {
        int selected_subject = 0;
        for (int index = 0; index < 5; index++) {
            if (!strcmp(subject, game.subjects[index])) selected_subject = 1;
        }
        if (!selected_subject) {
            http_respond(client, 400, "Bad Request", "{\"error\":\"Flip must use one of this game's selected subjects\"}");
            return;
        }
    }
    if (!load_question(id, game.question_number, &question)) {
        http_respond(client, 404, "Not Found", "{\"error\":\"Question not found\"}");
        return;
    }

    sqlite3 *database = database_connection();
    if (sqlite3_exec(database, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK) {
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not apply lifeline\"}");
        return;
    }
    sqlite3_int64 replacement_id = 0;
    if (flip) replacement_id = flip_question_id(database, id, game.question_number, subject, question.id);
    if (flip && !replacement_id) {
        sqlite3_exec(database, "ROLLBACK", NULL, NULL, NULL);
        http_respond(client, 404, "Not Found", "{\"error\":\"No unused question is available in that subject\"}");
        return;
    }

    sqlite3_stmt *statement = NULL;
    if (sqlite3_prepare_v2(database,
        "INSERT INTO game_lifelines (game_id, lifeline, question_number, attempts_used) VALUES (?, ?, ?, 0)",
        -1, &statement, NULL) != SQLITE_OK) {
        sqlite3_exec(database, "ROLLBACK", NULL, NULL, NULL);
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not save lifeline use\"}");
        return;
    }
    sqlite3_bind_int64(statement, 1, id);
    sqlite3_bind_text(statement, 2, lifeline, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 3, game.question_number);
    int success = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!success) {
        sqlite3_exec(database, "ROLLBACK", NULL, NULL, NULL);
        http_respond(client, 409, "Conflict", "{\"error\":\"That lifeline has already been used\"}");
        return;
    }
    if (flip && sqlite3_prepare_v2(database,
        "UPDATE game_questions SET question_id = ? WHERE game_id = ? AND question_number = ?",
        -1, &statement, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(statement, 1, replacement_id);
        sqlite3_bind_int64(statement, 2, id);
        sqlite3_bind_int(statement, 3, game.question_number);
        success = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database) == 1;
        sqlite3_finalize(statement);
    } else if (flip) success = 0;
    if (!success || sqlite3_exec(database, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_exec(database, "ROLLBACK", NULL, NULL, NULL);
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not apply lifeline\"}");
        return;
    }

    if (flip) {
        char response[4096];
        if (!load_question(id, game.question_number, &question) ||
            !serialize_question(id, game.question_number, game.total, &question, response, sizeof(response))) {
            http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not load replacement question\"}");
            return;
        }
        http_respond(client, 200, "OK", response);
    } else if (expert) {
        char explanation[1536], response[2048];
        json_escape(question.explanation, explanation, sizeof(explanation));
        snprintf(response, sizeof(response), "{\"lifeline\":\"expert\",\"correctAnswer\":\"%s\",\"explanation\":\"%s\"}", question.answer, explanation);
        http_respond(client, 200, "OK", response);
    } else if (fifty_fifty) {
        char wrong[3];
        int count = 0;
        for (int index = 0; index < 4; index++) {
            if (answer_letters[index] != question.answer[0]) wrong[count++] = answer_letters[index];
        }
        unsigned int random_value = 0;
        sqlite3_randomness(sizeof(random_value), &random_value);
        int first = (int)(random_value % 3);
        random_value = 0;
        sqlite3_randomness(sizeof(random_value), &random_value);
        int second = (int)(random_value % 2);
        if (second >= first) second++;
        char response[160];
        snprintf(response, sizeof(response), "{\"lifeline\":\"fifty_fifty\",\"removedOptions\":[\"%c\",\"%c\"]}", wrong[first], wrong[second]);
        http_respond(client, 200, "OK", response);
    } else {
        http_respond(client, 200, "OK", "{\"lifeline\":\"double_dip\",\"attempts\":2}");
    }
}

void game_finish(socket_t client, const char *body, int user_id) {
    long id = 0;
    GameRecord game;
    if (!game_identifier(body, &id)) {
        http_respond(client, 400, "Bad Request", "{\"error\":\"A valid gameId is required\"}");
        return;
    }
    if (!load_game(id, user_id, &game)) {
        http_respond(client, 404, "Not Found", "{\"error\":\"Game not found\"}");
        return;
    }
    const char *status = game.status;
    if (!strcmp(game.status, "active")) {
        sqlite3_stmt *statement = NULL;
        if (sqlite3_prepare_v2(database_connection(), "UPDATE games SET status = 'quit' WHERE id = ? AND status = 'active'", -1, &statement, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(statement, 1, id);
            sqlite3_step(statement);
            sqlite3_finalize(statement);
            status = "quit";
        }
    }
    char response[256];
    snprintf(response, sizeof(response), "{\"correctAnswers\":%d,\"totalQuestions\":%d,\"status\":\"%s\"}", game.correct_answers, game.total, status);
    http_respond(client, 200, "OK", response);
}

void game_history(socket_t client, int user_id) {
    sqlite3_stmt *statement = NULL;
    const char *sql = "SELECT player_name, mode, status, correct_answers, total_questions, created_at "
        "FROM games WHERE user_id = ? ORDER BY id DESC LIMIT 30";
    if (sqlite3_prepare_v2(database_connection(), sql, -1, &statement, NULL) != SQLITE_OK) {
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not load game history\"}");
        return;
    }
    sqlite3_bind_int(statement, 1, user_id);
    char response[24000];
    size_t used = (size_t)snprintf(response, sizeof(response), "{\"games\":[");
    int count = 0, failed = 0;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        char player[160], created[96];
        const char *raw_player = (const char *)sqlite3_column_text(statement, 0);
        const char *raw_created = (const char *)sqlite3_column_text(statement, 5);
        if (!json_escape(raw_player ? raw_player : "Player", player, sizeof(player)) ||
            !json_escape(raw_created ? raw_created : "", created, sizeof(created))) { failed = 1; break; }
        int length = snprintf(response + used, sizeof(response) - used,
            "%s{\"playerName\":\"%s\",\"mode\":\"%s\",\"status\":\"%s\",\"correctAnswers\":%d,\"totalQuestions\":%d,\"createdAt\":\"%s\"}",
            count ? "," : "", player, sqlite3_column_text(statement, 1), sqlite3_column_text(statement, 2),
            sqlite3_column_int(statement, 3), sqlite3_column_int(statement, 4), created);
        if (length < 0 || (size_t)length >= sizeof(response) - used) { failed = 1; break; }
        used += (size_t)length;
        count++;
    }
    sqlite3_finalize(statement);
    if (failed || used + 3 >= sizeof(response)) {
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Game history is too large to return\"}");
        return;
    }
    memcpy(response + used, "]}", 3);
    http_respond(client, 200, "OK", response);
}
