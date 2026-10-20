#include <stdio.h>
#include <stdlib.h>
#include "kbc/storage/database.h"
#include "kbc/api/http.h"

#ifdef _WIN32
#include <winsock2.h>
#endif

int main(void) {
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        fprintf(stderr, "Winsock initialization failed.\n");
        return 1;
    }
#endif
    const char *database_path = getenv("KBC_DB_PATH");
    if (!database_path || !*database_path) database_path = "kbc.db";
    if (!database_initialize(database_path)) {
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    int result = http_server_run();
    database_close();
#ifdef _WIN32
    WSACleanup();
#endif
    return result;
}
