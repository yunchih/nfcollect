#include "sql.h"
#include "collect.h"
#include "extract.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static inline int _db_handle_result(sqlite3 *db, int rc, const char *errmsg,
                                    const char *err, bool fatal) {
    if (SQLITE_OK != rc) {
        ERROR("sqlite3: %s(%i): %s\n", errmsg ? errmsg : "", rc,
              err ? err : sqlite3_errmsg(db));
        if (fatal) {
            sqlite3_close(db);
            exit(1);
        }
    }
    return rc;
}

static inline int _db_exec(sqlite3 *db, const char *cmd, const char *errmsg,
                           bool fatal) {
    char *err = NULL;
    int rc = sqlite3_exec(db, cmd, NULL, NULL, &err);
    return _db_handle_result(db, rc, errmsg, err, fatal);
}

static inline int db_exec_fatal(sqlite3 *db, const char *cmd,
                                const char *errmsg) {
    return _db_exec(db, cmd, errmsg, true);
}

static inline int db_exec(sqlite3 *db, const char *cmd, const char *errmsg) {
    return _db_exec(db, cmd, errmsg, false);
}

static inline int db_prepare(sqlite3 *db, const char *cmd, const char *errmsg,
                             sqlite3_stmt **stmt) {
    int rc = sqlite3_prepare_v2(db, cmd, -1, stmt, 0);
    return _db_handle_result(db, rc, errmsg, NULL, true);
}

int db_set_pragma(sqlite3 *db) {
    return db_exec_fatal(db,
                         "PRAGMA journal_mode=WAL;"
                         "PRAGMA foreign_keys = ON;",
                         "Can't set Sqlite3 PRAGMA");
}

int db_vacuum(sqlite3 *db) {
    return db_exec(db, "VACUUM", "Can't vacuum database");
}

int db_create_table(sqlite3 *db) {
    const char *create_sql =
        "CREATE TABLE IF NOT EXISTS " g_sqlite_table_data " ("
        "id INTEGER PRIMARY KEY,"
        "data BLOB"
        ");"
        "CREATE TABLE IF NOT EXISTS " g_sqlite_table_header " ("
        "id INTEGER PRIMARY KEY,"
        "nr_entries INTEGER,"
        "size INTEGER,"
        "compression_type INTEGER,"
        "start_time INTEGER,"
        "end_time INTEGER,"
        "data_id INTEGER,"
        "FOREIGN KEY(data_id) REFERENCES " g_sqlite_table_data
        "(id) ON DELETE SET NULL"
        ");";
    int rc = 0, retry = g_sqlite_nr_fail_retry;
    while (retry--) {
        rc = db_exec(db, create_sql, "Can't create table");
        if (SQLITE_LOCKED != rc && SQLITE_BUSY != rc)
            return rc;
        sleep(1);
    }

    ERROR("Can't create table, reach max retry, bailed out!");
    exit(1);
}

int db_open(sqlite3 **db, const char *dbname) {
    int rc;
    rc = sqlite3_open(dbname, db);
    if (SQLITE_OK != rc) {
        ERROR("Can't open database %s (%i): %s\n", dbname, rc,
              sqlite3_errmsg(*db));
        exit(1);
    }

    return db_set_pragma(*db);
}

int db_close(sqlite3 *db) {
    sqlite3_close(db);
    return 0;
}

int db_insert(sqlite3 *db, const Header *header, const Entry *entries) {
    int rc;
    sqlite3_stmt *stmt[2] = {0};
    const char *insert_sql[] = {
        "INSERT INTO " g_sqlite_table_data " (data) VALUES(?)",
        "INSERT INTO " g_sqlite_table_header " "
        "(nr_entries, size, compression_type, start_time, end_time, data_id) "
        "VALUES(?, ?, ?, ?, ?, ?)"};

    db_exec_fatal(db, "BEGIN TRANSACTION", "db_insert: Can't begin txn");
    for (int i = 0; i < 2;) {
        rc = db_prepare(db, insert_sql[i], "Can't insert data", &stmt[i]);
        if (i == 0) {
            sqlite3_bind_blob(stmt[i], 1, entries, header->raw_size,
                              SQLITE_STATIC);
        } else {
            sqlite3_int64 data_id = sqlite3_last_insert_rowid(db);
            sqlite3_bind_int(stmt[i], 1, header->nr_entries);
            sqlite3_bind_int(stmt[i], 2, header->raw_size);
            sqlite3_bind_int(stmt[i], 3, header->compression_type);
            sqlite3_bind_int64(stmt[i], 4, header->start_time);
            sqlite3_bind_int64(stmt[i], 5, header->end_time);
            sqlite3_bind_int64(stmt[i], 6, data_id);
        }

        rc = sqlite3_step(stmt[i]);
        if (rc != SQLITE_DONE)
            WARN("sqlite3: Insert data step fail: %d\n", rc);

        if (SQLITE_SCHEMA == sqlite3_finalize(stmt[i]))
            continue;
        i++;
    }

    DEBUG("Inserted #%d of compressed size %d", header->nr_entries,
          header->raw_size);
    db_exec_fatal(db, "END TRANSACTION", "db_insert: Can't end txn");
    return rc;
}

int db_read_data_by_timerange(sqlite3 *db, const Timerange *t,
                              StateCallback cb) {
    const char *_select_sql =
        "SELECT * FROM " g_sqlite_table_header
        " INNER JOIN " g_sqlite_table_data " ON " g_sqlite_table_header
        ".data_id = " g_sqlite_table_data ".id"
        " WHERE " g_sqlite_table_header
        ".end_time > %ld AND " g_sqlite_table_header ".start_time < %ld";
    char select_sql[strlen(_select_sql) + 25];
    sprintf(select_sql, _select_sql, t->from, t->until);

    sqlite3_stmt *stmt;
    db_exec_fatal(db, "BEGIN TRANSACTION", "db_delete: Can't begin txn");
    int rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        ERROR("Can't select (%i): %s\n", rc, sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    int count = 0;
    for (;; count++) {
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE)
            break;
        assert(rc == SQLITE_ROW);

        State *s = malloc(sizeof(State));
        s->header = malloc(sizeof(Header));
        s->header->nr_entries = sqlite3_column_int(stmt, 1);
        s->header->raw_size = sqlite3_column_int(stmt, 2);
        s->header->compression_type = sqlite3_column_int(stmt, 3);

        size_t size = sqlite3_column_bytes(stmt, 8);
        DEBUG("extract: nr_entries: %d "
              "raw_size: %d "
              "compression_type: %d "
              "size: %ld",
              s->header->nr_entries, s->header->raw_size,
              s->header->compression_type, size);
        if (size != (size_t)s->header->raw_size)
            FATAL("extract: header data size and actual size not match: "
                  "expected: %u, got: %ld",
                  s->header->raw_size, size);

        bool ok = extract(s, sqlite3_column_blob(stmt, 8));
        if (ok)
            cb(s, t);
        state_free(s);
    }

    assert(SQLITE_SCHEMA != sqlite3_finalize(stmt));
    db_exec_fatal(db, "END TRANSACTION", "db_begin: Can't end txn");

    return count;
}

int db_get_space_consumed(sqlite3 *db) {
    const char *select_data_sql = "SELECT SUM(size) FROM " g_sqlite_table_data;
    const char *select_header_sql =
        "SELECT COUNT(*) FROM " g_sqlite_table_header;

    int size;
    sqlite3_stmt *stmt = NULL;
    db_prepare(db, select_data_sql, "Can't query data", &stmt);
    size = sqlite3_column_int64(stmt, 1);

    db_prepare(db, select_header_sql, "Can't query data", &stmt);
    size += sizeof(Header) * sqlite3_column_int64(stmt, 1);
    return size;
}

int db_delete_oldest_bytes(sqlite3 *db, int64_t bytes) {
    int rc;
    sqlite3_stmt *stmt;
    const char *select_sql =
        "SELECT size, end_time, data_id "
        "FROM " g_sqlite_table_header " WHERE data_id IS NOT NULL "
        "ORDER BY end_time";
    if (!bytes)
        return 0;

    db_exec_fatal(db, "BEGIN TRANSACTION", "db_delete: Can't begin txn");
    rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        ERROR("Can't select (%i): %s\n", rc, sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    int count = 0;
    size_t bufsize = 1024;
    char *buf = malloc(bufsize);

    while (bytes >= 0) {
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE)
            break;
        assert(rc == SQLITE_ROW);
        sqlite3_int64 index = sqlite3_column_int64(stmt, 2);
        int size = sqlite3_column_int(stmt, 0);

        char _buf[22];
        sprintf(_buf, count ? "%lld" : ",%lld", index);
        while (strlen(_buf) + strlen(buf) + 2 >= bufsize) {
            bufsize *= 2;
            char *__buf = malloc(bufsize);
            memcpy(__buf, buf, strlen(buf) + 1);
            free(buf);
            buf = __buf;
        }

        strcat(buf, _buf);
        bytes -= size;
        count++;
    }

    if (!*buf)
        return 0;

    const char *_delete_sql =
        "DELETE FROM " g_sqlite_table_data " WHERE id in (%s);";
    char *delete_sql = malloc(strlen(_delete_sql) + strlen(buf) + 1);
    sprintf(delete_sql, _delete_sql, buf);
    db_exec_fatal(db, delete_sql, "Can't delete");
    DEBUG("Deleted old data, SQL: %s", delete_sql);
    free(delete_sql);
    free(buf);

    assert(SQLITE_SCHEMA != sqlite3_finalize(stmt));
    db_exec_fatal(db, "END TRANSACTION", "db_begin: Can't end txn");

    return count;
}
