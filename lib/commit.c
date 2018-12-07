#include "collect.h"
#include "main.h"
#include "sql.h"

#include <zstd.h>

static void do_gc(sqlite3 *db, State *s) {
    uint32_t cur_size = s->header->raw_size;
    pthread_mutex_lock(&s->global->storage_consumed_lock);
    uint32_t remain_size =
        s->global->storage_budget - s->global->storage_consumed - cur_size;
    uint32_t gc_size = -remain_size + cur_size * g_gc_rate;
    if (gc_size >= s->global->storage_consumed)
        gc_size = s->global->storage_consumed;
    pthread_mutex_unlock(&s->global->storage_consumed_lock);

    if (remain_size <= 0)
        db_delete_oldest_bytes(db, gc_size);
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
