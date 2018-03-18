#include "commit.h"
#include <errno.h>
#include <string.h>
#include <zstd.h>

static void nfl_commit_default(FILE *f, nfl_entry_t *store,
                               uint32_t store_size);
static void nfl_commit_lz4(FILE *f, nfl_entry_t *store, uint32_t store_size);
static void nfl_commit_zstd(FILE *f, nfl_entry_t *store, uint32_t store_size);

void nfl_commit_init() { /* TODO */ }

static void nfl_commit_default(FILE *f, nfl_entry_t *store,
                               uint32_t store_size) {
    uint32_t written;
    written = fwrite(store, 1, store_size, f);
    ERR(written != store_size, strerror(errno));
}

static void nfl_commit_lz4(FILE *f, nfl_entry_t *store, uint32_t store_size) {
    /* TODO */
}

static void nfl_commit_zstd(FILE *f, nfl_entry_t *store,
                            uint32_t store_size) {
    size_t const bufsize = ZSTD_compressBound(store_size);
    void *buf;

    ERR(!(buf = malloc(bufsize)), "zstd: cannot malloc");
    size_t const csize = ZSTD_compress(buf, bufsize, store, store_size, 1);
    if (ZSTD_isError(csize))
        FATAL("zstd: %s \n", ZSTD_getErrorName(csize));

    nfl_commit_default(f, buf, csize);
    free(buf);
}

void nfl_commit_worker(nfl_header_t *header, nfl_entry_t *store,
                       enum nfl_compression_t compression_opt,
                       const char *filename) {
    FILE *f;
    uint32_t written;

    debug("Comm worker #%u: commit to file %s\n", header->id, filename);
    ERR((f = fopen(filename, "wb")) == NULL, strerror(errno));

    // commit header
    written = fwrite(header, 1, sizeof(nfl_header_t), f);
    ERR(written != sizeof(nfl_header_t), strerror(errno));

    // commit store
    uint32_t store_size = sizeof(nfl_entry_t) * header->max_n_entries;
    switch(compression_opt) {
        case COMPRESS_NONE:
            debug("Comm worker #%u: commit without compression\n", header->id)
            nfl_commit_default(f, store, store_size);
            break;
        case COMPRESS_LZ4:
            debug("Comm worker #%u: commit with compression algorithm: lz4", header->id)
            nfl_commit_lz4(f, store, store_size);
            break;
        case COMPRESS_ZSTD:
            debug("Comm worker #%u: commit with compression algorithm: zstd", header->id)
            nfl_commit_zstd(f, store, store_size);
            break;
        // Must not reach here ...
        default: FATAL("Unknown compression option detected");
    }

    // Do fsync ?
    fclose(f);
}
