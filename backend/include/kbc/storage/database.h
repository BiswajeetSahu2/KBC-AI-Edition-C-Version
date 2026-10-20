#ifndef KBC_DATABASE_H
#define KBC_DATABASE_H

#include "sqlite3.h"

int database_initialize(const char *path);
void database_close(void);
sqlite3 *database_connection(void);

#endif