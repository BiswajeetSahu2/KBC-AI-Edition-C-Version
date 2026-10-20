#include <stdio.h>
#include <string.h>
#include "kbc/storage/database.h"
#include "kbc/game/question_bank.h"

static sqlite3 *connection;

static int games_have_user_id(void) {
    sqlite3_stmt *statement = NULL;
    if (sqlite3_prepare_v2(connection, "PRAGMA table_info(games)", -1, &statement, NULL) != SQLITE_OK) return 0;
    int found = 0;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(statement, 1);
        if (name && !strcmp((const char *)name, "user_id")) {
            found = 1;
            break;
        }
    }
    sqlite3_finalize(statement);
    return found;
}

static int users_have_guest_flag(void) {
    sqlite3_stmt *statement = NULL;
    if (sqlite3_prepare_v2(connection, "PRAGMA table_info(users)", -1, &statement, NULL) != SQLITE_OK) return 0;
    int found = 0;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(statement, 1);
        if (name && !strcmp((const char *)name, "is_guest")) {
            found = 1;
            break;
        }
    }
    sqlite3_finalize(statement);
    return found;
}

static int users_have_column(const char *column_name) {
    sqlite3_stmt *statement = NULL;
    if (sqlite3_prepare_v2(connection, "PRAGMA table_info(users)", -1, &statement, NULL) != SQLITE_OK) return 0;
    int found = 0;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(statement, 1);
        if (name && !strcmp((const char *)name, column_name)) { found = 1; break; }
    }
    sqlite3_finalize(statement);
    return found;
}

int database_initialize(const char *path) {
    if (sqlite3_open(path, &connection) != SQLITE_OK) {
        fprintf(stderr, "Could not open SQLite database: %s\n", sqlite3_errmsg(connection));
        return 0;
    }
    sqlite3_busy_timeout(connection, 3000);
    const char *schema =
        "PRAGMA foreign_keys = ON;"
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY, username TEXT NOT NULL COLLATE NOCASE UNIQUE,"
        "password_salt BLOB NOT NULL, password_hash BLOB NOT NULL, recovery_hash BLOB, profile_picture TEXT,"
        "is_guest INTEGER NOT NULL DEFAULT 0,"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);"
        "CREATE TABLE IF NOT EXISTS user_sessions ("
        "token_hash BLOB PRIMARY KEY, user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, expires_at TEXT NOT NULL);"
        "CREATE INDEX IF NOT EXISTS user_sessions_expiration ON user_sessions(expires_at);"
        "CREATE TABLE IF NOT EXISTS questions ("
        "id INTEGER PRIMARY KEY, subject TEXT NOT NULL, question TEXT NOT NULL,"
        "option_a TEXT NOT NULL, option_b TEXT NOT NULL, option_c TEXT NOT NULL, option_d TEXT NOT NULL,"
        "answer TEXT NOT NULL, explanation TEXT NOT NULL, UNIQUE(subject, question));"
        "CREATE TABLE IF NOT EXISTS games ("
        "id INTEGER PRIMARY KEY, user_id INTEGER REFERENCES users(id), player_name TEXT NOT NULL, mode TEXT NOT NULL,"
        "total_questions INTEGER NOT NULL, question_number INTEGER NOT NULL DEFAULT 1,"
        "correct_answers INTEGER NOT NULL DEFAULT 0, status TEXT NOT NULL DEFAULT 'active',"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);"
        "CREATE TABLE IF NOT EXISTS game_subjects ("
        "game_id INTEGER NOT NULL REFERENCES games(id), position INTEGER NOT NULL, subject TEXT NOT NULL,"
        "PRIMARY KEY(game_id, position));"
        "CREATE TABLE IF NOT EXISTS game_questions ("
        "game_id INTEGER NOT NULL REFERENCES games(id) ON DELETE CASCADE, question_number INTEGER NOT NULL,"
        "question_id INTEGER NOT NULL REFERENCES questions(id), PRIMARY KEY(game_id, question_number));"
        "CREATE INDEX IF NOT EXISTS game_questions_question_id ON game_questions(question_id);"
        "CREATE TABLE IF NOT EXISTS game_answers ("
        "game_id INTEGER NOT NULL REFERENCES games(id), question_number INTEGER NOT NULL,"
        "question_id INTEGER NOT NULL REFERENCES questions(id), selected_answer TEXT NOT NULL,"
        "is_correct INTEGER NOT NULL, answered_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "PRIMARY KEY(game_id, question_number));"
        "CREATE TABLE IF NOT EXISTS game_lifelines ("
        "game_id INTEGER NOT NULL REFERENCES games(id) ON DELETE CASCADE, lifeline TEXT NOT NULL,"
        "question_number INTEGER NOT NULL, attempts_used INTEGER NOT NULL DEFAULT 0,"
        "used_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY(game_id, lifeline));"
        "UPDATE questions SET question = 'Which body of water borders India''s eastern coast?', "
        "option_a = 'Arabian Sea', option_b = 'Bay of Bengal', option_c = 'Red Sea', "
        "option_d = 'Persian Gulf', answer = 'B', "
        "explanation = 'India''s eastern coastline borders the Bay of Bengal.' "
        "WHERE subject = 'Geography' AND question = 'Which ocean borders India''s eastern coast?';";
    if (sqlite3_exec(connection, schema, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "Could not initialize SQLite schema: %s\n", sqlite3_errmsg(connection));
        database_close();
        return 0;
    }
    if (!games_have_user_id() && sqlite3_exec(connection,
        "ALTER TABLE games ADD COLUMN user_id INTEGER REFERENCES users(id)", NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "Could not migrate game account ownership: %s\n", sqlite3_errmsg(connection));
        database_close();
        return 0;
    }
    if (!users_have_guest_flag() && sqlite3_exec(connection,
        "ALTER TABLE users ADD COLUMN is_guest INTEGER NOT NULL DEFAULT 0", NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "Could not migrate guest account support: %s\n", sqlite3_errmsg(connection));
        database_close();
        return 0;
    }
    if (!users_have_column("recovery_hash") && sqlite3_exec(connection,
        "ALTER TABLE users ADD COLUMN recovery_hash BLOB", NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "Could not migrate account recovery support: %s\n", sqlite3_errmsg(connection));
        database_close();
        return 0;
    }
    if (!users_have_column("profile_picture") && sqlite3_exec(connection,
        "ALTER TABLE users ADD COLUMN profile_picture TEXT", NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "Could not migrate profile picture support: %s\n", sqlite3_errmsg(connection));
        database_close();
        return 0;
    }

    sqlite3_stmt *statement = NULL;
    const char *insert = "INSERT OR IGNORE INTO questions "
        "(subject, question, option_a, option_b, option_c, option_d, answer, explanation) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(connection, insert, -1, &statement, NULL) != SQLITE_OK) {
        fprintf(stderr, "Could not prepare question seed: %s\n", sqlite3_errmsg(connection));
        database_close();
        return 0;
    }
    for (size_t index = 0; index < question_bank_count; index++) {
        const Question *question = &question_bank[index];
        sqlite3_bind_text(statement, 1, question->subject, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 2, question->question, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 3, question->a, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 4, question->b, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 5, question->c, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 6, question->d, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 7, question->answer, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 8, question->explanation, -1, SQLITE_STATIC);
        if (sqlite3_step(statement) != SQLITE_DONE) {
            fprintf(stderr, "Could not seed question bank: %s\n", sqlite3_errmsg(connection));
            sqlite3_finalize(statement);
            database_close();
            return 0;
        }
        sqlite3_reset(statement);
        sqlite3_clear_bindings(statement);
    }
    sqlite3_finalize(statement);
    return 1;
}

void database_close(void) {
    if (connection) {
        sqlite3_close(connection);
        connection = NULL;
    }
}

sqlite3 *database_connection(void) {
    return connection;
}
