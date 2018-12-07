#ifndef SQL_H
#define SQL_H

#include "main.h"
#include <sqlite3.h>

int db_set_pragma(sqlite3 *db);
int db_vacuum(sqlite3 *db);
int db_create_table(sqlite3 *db);
int db_open(sqlite3 **db, const char *dbname);
int db_close(sqlite3 *db);
int db_insert(sqlite3 *db, const Header *header, const Entry *entries);
int db_get_space_consumed(sqlite3 *db);
int db_delete_oldest_bytes(sqlite3 *db, int64_t bytes);
int db_read_data_by_timerange(sqlite3 *db, const Timerange *t,
                              StateCallback cb);

#endif // SQL_H
