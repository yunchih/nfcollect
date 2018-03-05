#include "commit.h"
#include <errno.h>
#include <string.h>
#include <zstd.h>

static void nfl_commit_default(FILE *f, nflog_entry_t *store,
                               uint32_t store_size);
static void nfl_commit_lz4(FILE *f, nflog_entry_t *store, uint32_t store_size);
static void nfl_commit_zstd(FILE *f, nflog_entry_t *store, uint32_t store_size);

typedef void (*nflog_commit_run_table_t)(FILE *f, nflog_entry_t *store,
                                         uint32_t size);
static const nflog_commit_run_table_t commit_run_table[] = {
    nfl_commit_default, nfl_commit_lz4, nfl_commit_zstd};

void nfl_commit_init() { /* TODO */ }

static void nfl_commit_default(FILE *f, nflog_entry_t *store,
                               uint32_t store_size) {
    uint32_t written;
    written = fwrite(store, 1, store_size, f);
    ERR(written != store_size, strerror(errno));
}

static void nfl_commit_lz4(FILE *f, nflog_entry_t *store, uint32_t store_size) {
    /* TODO */
}

static void nfl_commit_zstd(FILE *f, nflog_entry_t *store,
                            uint32_t store_size) {
    size_t const bufsize = ZSTD_compressBound(store_size);
    void *buf;

    ERR((buf = malloc(bufsize)), NULL);

    size_t const csize = ZSTD_compress(buf, bufsize, store, store_size, 1);
    if (ZSTD_isError(csize)) {
        fprintf(stderr, "zstd error: %s \n", ZSTD_getErrorName(csize));
        exit(8);
    }

    nfl_commit_default(f, buf, bufsize);
    free(buf);
}

void nfl_commit_worker(nflog_header_t *header, nflog_entry_t *store,
                       const char *filename) {
    FILE *f;
    uint32_t written;

    debug("Comm worker #%u: commit to file %s\n", header->id, filename);
    ERR((f = fopen(filename, "wb")) == NULL, strerror(errno));

    // commit header
    written = fwrite(header, 1, sizeof(nflog_header_t), f);
    ERR(written != sizeof(nflog_header_t), strerror(errno));

    // commit store
    uint32_t store_size = sizeof(nflog_entry_t) * header->max_n_entries;
    commit_run_table[header->compression_opt](f, store, store_size);

    // Do fsync ?
    fclose(f);
}
