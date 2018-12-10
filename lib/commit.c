#include "collect.h"
#include "main.h"
#include "sql.h"
#include "util.h"

#include <zstd.h>

static void do_gc(sqlite3 *db, State *s) {
    int64_t cur_size = (int64_t)s->header->raw_size;
    pthread_mutex_lock(&s->global->storage_consumed_lock);
    int64_t remain_size =
        s->global->storage_budget - s->global->storage_consumed - cur_size;
    int64_t gc_size = 0;
    if (remain_size <= 0) {
        gc_size = -remain_size + cur_size * g_gc_rate;
        if (gc_size >= s->global->storage_consumed)
            gc_size = s->global->storage_consumed * g_gc_cap;
        else if (gc_size >= s->global->storage_budget * g_gc_cap)
            gc_size = s->global->storage_budget * g_gc_cap;
    }
    DEBUG("do_gc: gc_size %.2f KB, remain %.2f KB, cur_size, %.2f KB\n",
          gc_size / 1024.0, remain_size / 1024.0, cur_size / 1024.0);
    pthread_mutex_unlock(&s->global->storage_consumed_lock);

    uint32_t gc_count = 0;
    if (gc_size > 0) {
        gc_count = db_delete_oldest_bytes(db, gc_size);
        db_vacuum(db);
    }

    int64_t dbsize = check_file_size(s->global->storage_file);
    pthread_mutex_lock(&s->global->storage_consumed_lock);
    s->global->storage_consumed = dbsize;
    pthread_mutex_unlock(&s->global->storage_consumed_lock);

    if (gc_count) {
        INFO("gc: storage budget: %.2f MB, storage consumed: %.2f MB, (%.2f "
             "MB/%d entries) vacuumed",
             s->global->storage_budget / 1024.0 / 1024.0,
             s->global->storage_consumed / 1024.0 / 1024.0,
             gc_size / 1024.0 / 1024.0, gc_count);
    } else {
        DEBUG("gc: storage budget: %.2f MB, storage consumed: %.2f MB, skip "
              "vacuuming",
              s->global->storage_budget / 1024.0 / 1024.0,
              s->global->storage_consumed / 1024.0 / 1024.0);
    }
}

static int commit_lz4(State *s, void **buf) {
    /* TODO */
    (void)s;
    (void)buf;
    return -1;
}

static int commit_zstd(State *s, void **buf) {
    size_t const bufsize = ZSTD_compressBound(s->header->raw_size);

    if (!(*buf = malloc(bufsize)))
        ERROR("zstd: cannot malloc");

    size_t const csize =
        ZSTD_compress(*buf, bufsize, s->store, s->header->raw_size, 0);
    if (ZSTD_isError(csize)) {
        ERROR("zstd: %s \n", ZSTD_getErrorName(csize));
        free(*buf);
        return -1;
    }

    s->header->raw_size = csize;
    return 0;
}

void *commit(void *targs) {
    sqlite3 *db = NULL;
    State *s = (State *)targs;
    uint32_t size = s->header->raw_size;
    DEBUG("Committing #%d packets", s->header->nr_entries);

    db_open(&db, s->global->storage_file);
    db_create_table(db);

    void *buf = NULL;
    switch (s->global->compression_type) {
    case COMPRESS_NONE:
        break;
    case COMPRESS_LZ4:
        commit_lz4(s, &buf);
        break;
    case COMPRESS_ZSTD:
        commit_zstd(s, &buf);
        break;
    default:
        FATAL("Unknown compression option detected");
    }

    do_gc(db, s);
    db_insert(db, s->header, buf ? buf : s->store);
    db_close(db);

    DEBUG("Committed #%d packets, compressed size: %u/%u",
          s->header->nr_entries, s->header->raw_size, size);
    if (buf)
        free(buf);
    state_free(s);

    return NULL;
}
