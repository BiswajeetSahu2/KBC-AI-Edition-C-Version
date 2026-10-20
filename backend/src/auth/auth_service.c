#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "kbc/api/http.h"
#include "kbc/api/request_parser.h"
#include "kbc/auth/auth_service.h"
#include "kbc/storage/database.h"

#define PASSWORD_SALT_SIZE 16
#define PASSWORD_HASH_SIZE 32
#define SESSION_TOKEN_SIZE 32
#define SESSION_TOKEN_HEX_SIZE (SESSION_TOKEN_SIZE * 2)
#define PASSWORD_ITERATIONS 150000

typedef LONG (WINAPI *cng_open_algorithm_fn)(PVOID *, LPCWSTR, LPCWSTR, ULONG);
typedef LONG (WINAPI *cng_close_algorithm_fn)(PVOID, ULONG);
typedef LONG (WINAPI *cng_derive_pbkdf2_fn)(PVOID, PUCHAR, ULONG, PUCHAR, ULONG, ULONGLONG, PUCHAR, ULONG, ULONG);
typedef LONG (WINAPI *cng_random_fn)(PVOID, PUCHAR, ULONG, ULONG);
typedef LONG (WINAPI *cng_get_property_fn)(PVOID, LPCWSTR, PUCHAR, ULONG, ULONG *, ULONG);
typedef LONG (WINAPI *cng_create_hash_fn)(PVOID, PVOID *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
typedef LONG (WINAPI *cng_hash_data_fn)(PVOID, PUCHAR, ULONG, ULONG);
typedef LONG (WINAPI *cng_finish_hash_fn)(PVOID, PUCHAR, ULONG, ULONG);
typedef LONG (WINAPI *cng_destroy_hash_fn)(PVOID);

static HMODULE cng_library;
static cng_open_algorithm_fn cng_open_algorithm;
static cng_close_algorithm_fn cng_close_algorithm;
static cng_derive_pbkdf2_fn cng_derive_pbkdf2;
static cng_random_fn cng_random;
static cng_get_property_fn cng_get_property;
static cng_create_hash_fn cng_create_hash;
static cng_hash_data_fn cng_hash_data;
static cng_finish_hash_fn cng_finish_hash;
static cng_destroy_hash_fn cng_destroy_hash;

static void secure_clear(void *memory, size_t length) {
    volatile unsigned char *bytes = (volatile unsigned char *)memory;
    while (length--) *bytes++ = 0;
}

static int cng_initialize(void) {
    if (cng_library) return 1;
    cng_library = LoadLibraryW(L"bcrypt.dll");
    if (!cng_library) return 0;
    cng_open_algorithm = (cng_open_algorithm_fn)GetProcAddress(cng_library, "BCryptOpenAlgorithmProvider");
    cng_close_algorithm = (cng_close_algorithm_fn)GetProcAddress(cng_library, "BCryptCloseAlgorithmProvider");
    cng_derive_pbkdf2 = (cng_derive_pbkdf2_fn)GetProcAddress(cng_library, "BCryptDeriveKeyPBKDF2");
    cng_random = (cng_random_fn)GetProcAddress(cng_library, "BCryptGenRandom");
    cng_get_property = (cng_get_property_fn)GetProcAddress(cng_library, "BCryptGetProperty");
    cng_create_hash = (cng_create_hash_fn)GetProcAddress(cng_library, "BCryptCreateHash");
    cng_hash_data = (cng_hash_data_fn)GetProcAddress(cng_library, "BCryptHashData");
    cng_finish_hash = (cng_finish_hash_fn)GetProcAddress(cng_library, "BCryptFinishHash");
    cng_destroy_hash = (cng_destroy_hash_fn)GetProcAddress(cng_library, "BCryptDestroyHash");
    if (!cng_open_algorithm || !cng_close_algorithm || !cng_derive_pbkdf2 || !cng_random ||
        !cng_get_property || !cng_create_hash || !cng_hash_data || !cng_finish_hash || !cng_destroy_hash) {
        FreeLibrary(cng_library);
        cng_library = NULL;
        return 0;
    }
    return 1;
}

static int valid_username(const char *username) {
    size_t length = strlen(username);
    if (length < 3 || length > 24) return 0;
    for (size_t index = 0; index < length; index++) {
        unsigned char character = (unsigned char)username[index];
        if (!isalnum(character) && character != '.' && character != '_' && character != '-') return 0;
    }
    return 1;
}

static int derive_password_hash(const char *password, const unsigned char *salt, unsigned char *hash) {
    if (!cng_initialize()) return 0;
    PVOID algorithm = NULL;
    LONG status = cng_open_algorithm(
        &algorithm, L"SHA256", NULL, 0x00000008);
    if (status >= 0) {
        status = cng_derive_pbkdf2(
            algorithm, (PUCHAR)password, (ULONG)strlen(password), (PUCHAR)salt,
            PASSWORD_SALT_SIZE, PASSWORD_ITERATIONS, hash, PASSWORD_HASH_SIZE, 0);
        cng_close_algorithm(algorithm, 0);
    }
    return status >= 0;
}

static int sha256(const unsigned char *data, ULONG data_length, unsigned char *hash) {
    if (!cng_initialize()) return 0;
    PVOID algorithm = NULL, hash_handle = NULL;
    PUCHAR hash_object = NULL;
    DWORD object_length = 0, result_length = 0;
    LONG status = cng_open_algorithm(&algorithm, L"SHA256", NULL, 0);
    if (status >= 0) {
        status = cng_get_property(algorithm, L"ObjectLength",
            (PUCHAR)&object_length, sizeof(object_length), &result_length, 0);
    }
    if (status >= 0) {
        hash_object = (PUCHAR)malloc(object_length);
        if (!hash_object) status = -1;
    }
    if (status >= 0) status = cng_create_hash(algorithm, &hash_handle, hash_object, object_length, NULL, 0, 0);
    if (status >= 0) status = cng_hash_data(hash_handle, (PUCHAR)data, data_length, 0);
    if (status >= 0) status = cng_finish_hash(hash_handle, hash, PASSWORD_HASH_SIZE, 0);
    if (hash_handle) cng_destroy_hash(hash_handle);
    if (hash_object) {
        secure_clear(hash_object, object_length);
        free(hash_object);
    }
    if (algorithm) cng_close_algorithm(algorithm, 0);
    return status >= 0;
}

static int token_hash(const char *token, unsigned char *hash) {
    if (!token || strlen(token) != SESSION_TOKEN_HEX_SIZE) return 0;
    return sha256((const unsigned char *)token, SESSION_TOKEN_HEX_SIZE, hash);
}

static int make_recovery_code(char *code, unsigned char *hash) {
    static const char hexadecimal[] = "0123456789ABCDEF";
    unsigned char random_bytes[10];
    if (!cng_initialize() || cng_random(NULL, random_bytes, sizeof(random_bytes), 0x00000002) < 0) return 0;
    for (size_t index = 0; index < sizeof(random_bytes); index++) {
        code[index * 2] = hexadecimal[random_bytes[index] >> 4];
        code[index * 2 + 1] = hexadecimal[random_bytes[index] & 15];
    }
    code[20] = '\0';
    int success = sha256((const unsigned char *)code, 20, hash);
    secure_clear(random_bytes, sizeof(random_bytes));
    return success;
}

static int create_session(sqlite3 *database, long user_id, char *token) {
    static const char hexadecimal[] = "0123456789abcdef";
    unsigned char random_bytes[SESSION_TOKEN_SIZE], hash[PASSWORD_HASH_SIZE];
    if (!cng_initialize() || cng_random(NULL, random_bytes, sizeof(random_bytes), 0x00000002) < 0) return 0;
    for (size_t index = 0; index < sizeof(random_bytes); index++) {
        token[index * 2] = hexadecimal[random_bytes[index] >> 4];
        token[index * 2 + 1] = hexadecimal[random_bytes[index] & 15];
    }
    token[SESSION_TOKEN_HEX_SIZE] = '\0';
    secure_clear(random_bytes, sizeof(random_bytes));
    if (!token_hash(token, hash)) return 0;

    sqlite3_exec(database, "DELETE FROM user_sessions WHERE expires_at <= CURRENT_TIMESTAMP", NULL, NULL, NULL);
    sqlite3_stmt *statement = NULL;
    const char *sql = "INSERT INTO user_sessions (token_hash, user_id, expires_at) "
        "VALUES (?, ?, datetime('now', '+30 days'))";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_blob(statement, 1, hash, sizeof(hash), SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 2, user_id);
    int success = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    secure_clear(hash, sizeof(hash));
    return success;
}

static void send_authenticated(socket_t client, const char *username, const char *token, int is_guest, const char *recovery_code, int status, const char *label) {
    char response[384];
    snprintf(response, sizeof(response),
        "{\"status\":\"authenticated\",\"username\":\"%s\",\"sessionToken\":\"%s\",\"isGuest\":%s%s%s%s}",
        is_guest ? "Guest" : username, token, is_guest ? "true" : "false",
        recovery_code ? ",\"recoveryCode\":\"" : "", recovery_code ? recovery_code : "", recovery_code ? "\"" : "");
    http_respond(client, status, label, response);
}

void auth_register(socket_t client, const char *body) {
    char username[25] = {0}, password[129] = {0};
    if (!json_string(body, "username", username, sizeof(username)) || !valid_username(username) ||
        !json_string(body, "password", password, sizeof(password)) || strlen(password) < 8 || strlen(password) > 128) {
        http_respond(client, 400, "Bad Request", "{\"error\":\"Use a 3-24 character username and a password of at least 8 characters\"}");
        return;
    }

    unsigned char salt[PASSWORD_SALT_SIZE], hash[PASSWORD_HASH_SIZE], recovery_hash[PASSWORD_HASH_SIZE];
    char recovery_code[21];
    if (!cng_initialize() || cng_random(NULL, salt, sizeof(salt), 0x00000002) < 0 ||
        !derive_password_hash(password, salt, hash) || !make_recovery_code(recovery_code, recovery_hash)) {
        secure_clear(password, sizeof(password));
        secure_clear(salt, sizeof(salt));
        secure_clear(hash, sizeof(hash));
        secure_clear(recovery_hash, sizeof(recovery_hash));
        secure_clear(recovery_code, sizeof(recovery_code));
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Password protection is unavailable\"}");
        return;
    }
    secure_clear(password, sizeof(password));

    sqlite3 *database = database_connection();
    sqlite3_stmt *statement = NULL;
    if (sqlite3_exec(database, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(database, "INSERT INTO users (username, password_salt, password_hash, recovery_hash) VALUES (?, ?, ?, ?)", -1, &statement, NULL) != SQLITE_OK) {
        sqlite3_exec(database, "ROLLBACK", NULL, NULL, NULL);
        secure_clear(hash, sizeof(hash));
        secure_clear(recovery_hash, sizeof(recovery_hash));
        secure_clear(salt, sizeof(salt));
        secure_clear(recovery_code, sizeof(recovery_code));
        secure_clear(password, sizeof(password));
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not create account\"}");
        return;
    }
    sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(statement, 2, salt, sizeof(salt), SQLITE_TRANSIENT);
    sqlite3_bind_blob(statement, 3, hash, sizeof(hash), SQLITE_TRANSIENT);
    sqlite3_bind_blob(statement, 4, recovery_hash, sizeof(recovery_hash), SQLITE_TRANSIENT);
    int result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    secure_clear(hash, sizeof(hash));
    secure_clear(recovery_hash, sizeof(recovery_hash));
    secure_clear(salt, sizeof(salt));
    if (result == SQLITE_CONSTRAINT) {
        sqlite3_exec(database, "ROLLBACK", NULL, NULL, NULL);
        http_respond(client, 409, "Conflict", "{\"error\":\"That username is already registered\"}");
        return;
    }
    if (result != SQLITE_DONE) {
        sqlite3_exec(database, "ROLLBACK", NULL, NULL, NULL);
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not create account\"}");
        return;
    }

    long user_id = (long)sqlite3_last_insert_rowid(database);
    char token[SESSION_TOKEN_HEX_SIZE + 1];
    if (!create_session(database, user_id, token) || sqlite3_exec(database, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_exec(database, "ROLLBACK", NULL, NULL, NULL);
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not start account session\"}");
        return;
    }
    send_authenticated(client, username, token, 0, recovery_code, 201, "Created");
    secure_clear(recovery_code, sizeof(recovery_code));
}

void auth_login(socket_t client, const char *body) {
    char username[25] = {0}, password[129] = {0};
    if (!json_string(body, "username", username, sizeof(username)) ||
        !json_string(body, "password", password, sizeof(password)) || strlen(password) > 128) {
        http_respond(client, 400, "Bad Request", "{\"error\":\"Username and password are required\"}");
        return;
    }
    sqlite3 *database = database_connection();
    sqlite3_stmt *statement = NULL;
    const char *sql = "SELECT id, password_salt, password_hash FROM users "
        "WHERE username = ? COLLATE NOCASE AND is_guest = 0";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, NULL) != SQLITE_OK) {
        secure_clear(password, sizeof(password));
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not verify account\"}");
        return;
    }
    sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT);
    int result = sqlite3_step(statement);
    long user_id = 0;
    unsigned char expected[PASSWORD_HASH_SIZE], actual[PASSWORD_HASH_SIZE];
    int valid = 0;
    if (result == SQLITE_ROW && sqlite3_column_bytes(statement, 1) == PASSWORD_SALT_SIZE &&
        sqlite3_column_bytes(statement, 2) == PASSWORD_HASH_SIZE) {
        const unsigned char *salt = (const unsigned char *)sqlite3_column_blob(statement, 1);
        const unsigned char *stored_hash = (const unsigned char *)sqlite3_column_blob(statement, 2);
        memcpy(expected, stored_hash, sizeof(expected));
        user_id = (long)sqlite3_column_int64(statement, 0);
        valid = derive_password_hash(password, salt, actual);
        unsigned char difference = 0;
        for (size_t index = 0; valid && index < sizeof(expected); index++) difference |= expected[index] ^ actual[index];
        valid = valid && difference == 0;
    }
    sqlite3_finalize(statement);
    secure_clear(password, sizeof(password));
    secure_clear(expected, sizeof(expected));
    secure_clear(actual, sizeof(actual));
    if (!valid) {
        http_respond(client, 401, "Unauthorized", "{\"error\":\"Username or password is incorrect\"}");
        return;
    }
    if (sqlite3_prepare_v2(database, "SELECT username FROM users WHERE id = ?", -1, &statement, NULL) != SQLITE_OK) {
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not start account session\"}");
        return;
    }
    sqlite3_bind_int64(statement, 1, user_id);
    result = sqlite3_step(statement);
    char canonical_username[25] = {0};
    if (result == SQLITE_ROW) snprintf(canonical_username, sizeof(canonical_username), "%s", sqlite3_column_text(statement, 0));
    sqlite3_finalize(statement);
    char token[SESSION_TOKEN_HEX_SIZE + 1];
    if (result != SQLITE_ROW || !create_session(database, user_id, token)) {
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not start account session\"}");
        return;
    }
    send_authenticated(client, canonical_username, token, 0, NULL, 200, "OK");
}

static int create_guest_user(sqlite3 *database, char *username, long *user_id) {
    static const char hexadecimal[] = "0123456789abcdef";
    sqlite3_stmt *statement = NULL;
    const char *sql = "INSERT INTO users (username, password_salt, password_hash, is_guest) VALUES (?, ?, ?, 1)";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, NULL) != SQLITE_OK) return 0;
    for (int attempt = 0; attempt < 5; attempt++) {
        unsigned char name_bytes[8], salt[PASSWORD_SALT_SIZE], placeholder_hash[PASSWORD_HASH_SIZE];
        if (cng_random(NULL, name_bytes, sizeof(name_bytes), 0x00000002) < 0 ||
            cng_random(NULL, salt, sizeof(salt), 0x00000002) < 0 ||
            cng_random(NULL, placeholder_hash, sizeof(placeholder_hash), 0x00000002) < 0) {
            secure_clear(name_bytes, sizeof(name_bytes));
            secure_clear(salt, sizeof(salt));
            secure_clear(placeholder_hash, sizeof(placeholder_hash));
            sqlite3_finalize(statement);
            return 0;
        }
        memcpy(username, "Guest-", 6);
        for (size_t index = 0; index < sizeof(name_bytes); index++) {
            username[6 + index * 2] = hexadecimal[name_bytes[index] >> 4];
            username[7 + index * 2] = hexadecimal[name_bytes[index] & 15];
        }
        username[22] = '\0';
        sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT);
        sqlite3_bind_blob(statement, 2, salt, sizeof(salt), SQLITE_TRANSIENT);
        sqlite3_bind_blob(statement, 3, placeholder_hash, sizeof(placeholder_hash), SQLITE_TRANSIENT);
        int result = sqlite3_step(statement);
        secure_clear(name_bytes, sizeof(name_bytes));
        secure_clear(salt, sizeof(salt));
        secure_clear(placeholder_hash, sizeof(placeholder_hash));
        if (result == SQLITE_DONE) {
            *user_id = (long)sqlite3_last_insert_rowid(database);
            sqlite3_finalize(statement);
            return 1;
        }
        sqlite3_reset(statement);
        sqlite3_clear_bindings(statement);
        if (result != SQLITE_CONSTRAINT) break;
    }
    sqlite3_finalize(statement);
    return 0;
}

void auth_guest(socket_t client) {
    if (!cng_initialize()) {
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Guest sessions are unavailable\"}");
        return;
    }
    sqlite3 *database = database_connection();
    if (sqlite3_exec(database, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK) {
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not start guest session\"}");
        return;
    }
    char username[24], token[SESSION_TOKEN_HEX_SIZE + 1];
    long user_id = 0;
    if (!create_guest_user(database, username, &user_id) || !create_session(database, user_id, token) ||
        sqlite3_exec(database, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_exec(database, "ROLLBACK", NULL, NULL, NULL);
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not start guest session\"}");
        return;
    }
    send_authenticated(client, username, token, 1, NULL, 201, "Created");
}

int auth_user_id(const char *token) {
    unsigned char hash[PASSWORD_HASH_SIZE];
    if (!token_hash(token, hash)) return 0;
    sqlite3_stmt *statement = NULL;
    sqlite3 *database = database_connection();
    const char *sql = "SELECT user_id FROM user_sessions WHERE token_hash = ? AND expires_at > CURRENT_TIMESTAMP";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, NULL) != SQLITE_OK) {
        secure_clear(hash, sizeof(hash));
        return 0;
    }
    sqlite3_bind_blob(statement, 1, hash, sizeof(hash), SQLITE_TRANSIENT);
    int user_id = sqlite3_step(statement) == SQLITE_ROW ? sqlite3_column_int(statement, 0) : 0;
    sqlite3_finalize(statement);
    secure_clear(hash, sizeof(hash));
    return user_id;
}

void auth_current_user(socket_t client, const char *token) {
    int user_id = auth_user_id(token);
    sqlite3_stmt *statement = NULL;
    if (!user_id || sqlite3_prepare_v2(database_connection(), "SELECT username, is_guest FROM users WHERE id = ?", -1, &statement, NULL) != SQLITE_OK) {
        http_respond(client, 401, "Unauthorized", "{\"error\":\"Sign in to continue\"}");
        return;
    }
    sqlite3_bind_int(statement, 1, user_id);
    if (sqlite3_step(statement) != SQLITE_ROW) {
        sqlite3_finalize(statement);
        http_respond(client, 401, "Unauthorized", "{\"error\":\"Sign in to continue\"}");
        return;
    }
    char response[96];
    int is_guest = sqlite3_column_int(statement, 1);
    const char *display_name = is_guest ? "Guest" : (const char *)sqlite3_column_text(statement, 0);
    snprintf(response, sizeof(response), "{\"username\":\"%s\",\"isGuest\":%s}",
        display_name, is_guest ? "true" : "false");
    sqlite3_finalize(statement);
    http_respond(client, 200, "OK", response);
}

void auth_profile_picture(socket_t client, const char *body, int user_id, int save) {
    sqlite3 *database = database_connection();
    sqlite3_stmt *statement = NULL;
    if (!save) {
        if (sqlite3_prepare_v2(database, "SELECT profile_picture FROM users WHERE id = ?", -1, &statement, NULL) != SQLITE_OK) {
            http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not load profile picture\"}");
            return;
        }
        sqlite3_bind_int(statement, 1, user_id);
        if (sqlite3_step(statement) != SQLITE_ROW) {
            sqlite3_finalize(statement);
            http_respond(client, 404, "Not Found", "{\"error\":\"Account not found\"}");
            return;
        }
        const unsigned char *picture = sqlite3_column_text(statement, 0);
        char response[18000];
        int length = snprintf(response, sizeof(response), "{\"profilePicture\":%s%s%s}",
            picture ? "\"" : "null", picture ? (const char *)picture : "", picture ? "\"" : "");
        sqlite3_finalize(statement);
        if (length < 0 || (size_t)length >= sizeof(response)) {
            http_respond(client, 500, "Internal Server Error", "{\"error\":\"Profile picture is too large\"}");
            return;
        }
        http_respond(client, 200, "OK", response);
        return;
    }

    char picture[16001] = {0};
    if (!json_string(body, "profilePicture", picture, sizeof(picture)) ||
        strncmp(picture, "data:image/jpeg;base64,", 23) || strlen(picture) > 15000) {
        http_respond(client, 400, "Bad Request", "{\"error\":\"Upload a JPEG profile picture smaller than 11 KB\"}");
        return;
    }
    for (const char *character = picture + 23; *character; character++) {
        if (!((*character >= 'A' && *character <= 'Z') || (*character >= 'a' && *character <= 'z') ||
            (*character >= '0' && *character <= '9') || *character == '+' || *character == '/' || *character == '=')) {
            http_respond(client, 400, "Bad Request", "{\"error\":\"Invalid profile picture encoding\"}");
            return;
        }
    }
    if (sqlite3_prepare_v2(database, "UPDATE users SET profile_picture = ? WHERE id = ?", -1, &statement, NULL) != SQLITE_OK) {
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not save profile picture\"}");
        return;
    }
    sqlite3_bind_text(statement, 1, picture, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 2, user_id);
    int success = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database) == 1;
    sqlite3_finalize(statement);
    http_respond(client, success ? 200 : 500, success ? "OK" : "Internal Server Error",
        success ? "{\"status\":\"saved\"}" : "{\"error\":\"Could not save profile picture\"}");
}

void auth_recovery_code(socket_t client, int user_id) {
    unsigned char hash[PASSWORD_HASH_SIZE];
    char code[21];
    if (!make_recovery_code(code, hash)) {
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not generate a recovery code\"}");
        return;
    }
    sqlite3_stmt *statement = NULL;
    if (sqlite3_prepare_v2(database_connection(), "UPDATE users SET recovery_hash = ? WHERE id = ? AND is_guest = 0", -1, &statement, NULL) != SQLITE_OK) {
        secure_clear(hash, sizeof(hash)); secure_clear(code, sizeof(code));
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not save recovery code\"}");
        return;
    }
    sqlite3_bind_blob(statement, 1, hash, sizeof(hash), SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 2, user_id);
    int success = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database_connection()) == 1;
    sqlite3_finalize(statement);
    secure_clear(hash, sizeof(hash));
    if (!success) {
        secure_clear(code, sizeof(code));
        http_respond(client, 403, "Forbidden", "{\"error\":\"Guest profiles cannot create recovery codes\"}");
        return;
    }
    char response[96];
    snprintf(response, sizeof(response), "{\"recoveryCode\":\"%s\"}", code);
    secure_clear(code, sizeof(code));
    http_respond(client, 200, "OK", response);
}

void auth_reset_password(socket_t client, const char *body) {
    char username[25] = {0}, code[21] = {0}, password[129] = {0};
    if (!json_string(body, "username", username, sizeof(username)) || !valid_username(username) ||
        !json_string(body, "recoveryCode", code, sizeof(code)) || strlen(code) != 20 ||
        !json_string(body, "newPassword", password, sizeof(password)) || strlen(password) < 8 || strlen(password) > 128) {
        http_respond(client, 400, "Bad Request", "{\"error\":\"Provide a valid username, 20-character recovery code, and password of at least 8 characters\"}");
        return;
    }
    unsigned char supplied_hash[PASSWORD_HASH_SIZE], expected_hash[PASSWORD_HASH_SIZE];
    if (!sha256((const unsigned char *)code, 20, supplied_hash)) {
        secure_clear(code, sizeof(code)); secure_clear(password, sizeof(password));
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Password reset is unavailable\"}");
        return;
    }
    sqlite3 *database = database_connection();
    sqlite3_stmt *statement = NULL;
    if (sqlite3_prepare_v2(database, "SELECT id, recovery_hash FROM users WHERE username = ? COLLATE NOCASE AND is_guest = 0", -1, &statement, NULL) != SQLITE_OK) {
        secure_clear(code, sizeof(code)); secure_clear(password, sizeof(password)); secure_clear(supplied_hash, sizeof(supplied_hash));
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not verify recovery code\"}");
        return;
    }
    sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT);
    int result = sqlite3_step(statement), user_id = 0, valid = 0;
    if (result == SQLITE_ROW && sqlite3_column_bytes(statement, 1) == PASSWORD_HASH_SIZE) {
        memcpy(expected_hash, sqlite3_column_blob(statement, 1), sizeof(expected_hash));
        user_id = sqlite3_column_int(statement, 0);
        unsigned char difference = 0;
        for (size_t index = 0; index < sizeof(expected_hash); index++) difference |= expected_hash[index] ^ supplied_hash[index];
        valid = difference == 0;
    }
    sqlite3_finalize(statement);
    secure_clear(expected_hash, sizeof(expected_hash)); secure_clear(supplied_hash, sizeof(supplied_hash)); secure_clear(code, sizeof(code));
    if (!valid) {
        secure_clear(password, sizeof(password));
        http_respond(client, 401, "Unauthorized", "{\"error\":\"Username or recovery code is incorrect\"}");
        return;
    }
    unsigned char salt[PASSWORD_SALT_SIZE], hash[PASSWORD_HASH_SIZE];
    if (cng_random(NULL, salt, sizeof(salt), 0x00000002) < 0 || !derive_password_hash(password, salt, hash)) {
        secure_clear(password, sizeof(password));
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not reset password\"}");
        return;
    }
    secure_clear(password, sizeof(password));
    int success = sqlite3_exec(database, "BEGIN IMMEDIATE", NULL, NULL, NULL) == SQLITE_OK &&
        sqlite3_prepare_v2(database, "UPDATE users SET password_salt = ?, password_hash = ?, recovery_hash = NULL WHERE id = ? AND recovery_hash IS NOT NULL", -1, &statement, NULL) == SQLITE_OK;
    if (success) {
        sqlite3_bind_blob(statement, 1, salt, sizeof(salt), SQLITE_TRANSIENT);
        sqlite3_bind_blob(statement, 2, hash, sizeof(hash), SQLITE_TRANSIENT);
        sqlite3_bind_int(statement, 3, user_id);
        success = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database) == 1;
        sqlite3_finalize(statement);
    }
    if (success && sqlite3_prepare_v2(database, "DELETE FROM user_sessions WHERE user_id = ?", -1, &statement, NULL) == SQLITE_OK) {
        sqlite3_bind_int(statement, 1, user_id);
        success = sqlite3_step(statement) == SQLITE_DONE;
        sqlite3_finalize(statement);
    } else success = 0;
    secure_clear(salt, sizeof(salt)); secure_clear(hash, sizeof(hash));
    if (!success || sqlite3_exec(database, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_exec(database, "ROLLBACK", NULL, NULL, NULL);
        http_respond(client, 409, "Conflict", "{\"error\":\"Could not reset password; request a new recovery code\"}");
        return;
    }
    http_respond(client, 200, "OK", "{\"status\":\"password_reset\"}");
}

void auth_logout(socket_t client, const char *token) {
    unsigned char hash[PASSWORD_HASH_SIZE];
    if (!token_hash(token, hash)) {
        http_respond(client, 401, "Unauthorized", "{\"error\":\"Sign in to continue\"}");
        return;
    }
    sqlite3 *database = database_connection();
    int user_id = auth_user_id(token);
    sqlite3_stmt *statement = NULL;
    int is_guest = 0;
    if (user_id && sqlite3_prepare_v2(database, "SELECT is_guest FROM users WHERE id = ?", -1, &statement, NULL) == SQLITE_OK) {
        sqlite3_bind_int(statement, 1, user_id);
        if (sqlite3_step(statement) == SQLITE_ROW) is_guest = sqlite3_column_int(statement, 0);
        sqlite3_finalize(statement);
    }
    if (sqlite3_prepare_v2(database, "DELETE FROM user_sessions WHERE token_hash = ?", -1, &statement, NULL) != SQLITE_OK) {
        secure_clear(hash, sizeof(hash));
        http_respond(client, 500, "Internal Server Error", "{\"error\":\"Could not sign out\"}");
        return;
    }
    sqlite3_bind_blob(statement, 1, hash, sizeof(hash), SQLITE_TRANSIENT);
    int success = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database) == 1;
    sqlite3_finalize(statement);
    secure_clear(hash, sizeof(hash));
    if (!success) {
        http_respond(client, 401, "Unauthorized", "{\"error\":\"Session has expired\"}");
        return;
    }
    if (is_guest && user_id) {
        sqlite3_stmt *remaining = NULL;
        int active_sessions = 0;
        if (sqlite3_prepare_v2(database, "SELECT COUNT(*) FROM user_sessions WHERE user_id = ? AND expires_at > CURRENT_TIMESTAMP", -1, &remaining, NULL) == SQLITE_OK) {
            sqlite3_bind_int(remaining, 1, user_id);
            if (sqlite3_step(remaining) == SQLITE_ROW) active_sessions = sqlite3_column_int(remaining, 0);
            sqlite3_finalize(remaining);
        }
        if (!active_sessions) {
            sqlite3_exec(database, "BEGIN IMMEDIATE", NULL, NULL, NULL);
            const char *cleanup_sql[] = {
                "DELETE FROM game_answers WHERE game_id IN (SELECT id FROM games WHERE user_id = ?)",
                "DELETE FROM game_questions WHERE game_id IN (SELECT id FROM games WHERE user_id = ?)",
                "DELETE FROM game_subjects WHERE game_id IN (SELECT id FROM games WHERE user_id = ?)",
                "DELETE FROM games WHERE user_id = ?",
                "DELETE FROM users WHERE id = ? AND is_guest = 1"
            };
            for (size_t index = 0; index < sizeof(cleanup_sql) / sizeof(cleanup_sql[0]); index++) {
                if (sqlite3_prepare_v2(database, cleanup_sql[index], -1, &statement, NULL) != SQLITE_OK) break;
                sqlite3_bind_int(statement, 1, user_id);
                sqlite3_step(statement);
                sqlite3_finalize(statement);
            }
            sqlite3_exec(database, "COMMIT", NULL, NULL, NULL);
        }
    }
    http_respond(client, 200, "OK", "{\"status\":\"signed_out\"}");
}
