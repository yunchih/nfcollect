#include "commit.h"
#include <errno.h>
#include <string.h>
#include <zstd.h>

static int nfl_commit_default(FILE *f, nfl_header_t *header, nfl_entry_t *store,
                               uint32_t store_size) {
    uint32_t written;
    header->raw_size = store_size;

    // Write header
    written = fwrite(header, 1, sizeof(nfl_header_t), f);
    WARN_RETURN(written != sizeof(nfl_header_t), "commit header: %s", strerror(errno));

    // Write store
    written = fwrite(store, 1, store_size, f);
    WARN_RETURN(written != store_size, "commit store: %s", strerror(errno));

    return sizeof(nfl_header_t) + store_size;
}

static int nfl_commit_lz4(FILE *f, nfl_header_t *header, nfl_entry_t *store,
                           uint32_t store_size) {
    /* TODO */
    return -1;
}

static int nfl_commit_zstd(FILE *f, nfl_header_t *header, nfl_entry_t *store,
                            uint32_t store_size) {
    size_t const bufsize = ZSTD_compressBound(store_size);
    void *buf;

    WARN_RETURN(!(buf = malloc(bufsize)), "zstd: cannot malloc");
    size_t const csize = ZSTD_compress(buf, bufsize, store, store_size, 1);
    if (ZSTD_isError(csize)) {
        WARN(1, "zstd: %s \n", ZSTD_getErrorName(csize));
        free(buf);
        return -1;
    }

    int ret = nfl_commit_default(f, header, buf, csize);
    free(buf);
    return ret;
}

int nfl_commit_worker(nfl_header_t *header, nfl_entry_t *store,
                       enum nfl_compression_t compression_opt,
                       bool truncate,
                       const char *filename) {
    int ret;
    FILE *f;
    const char *mode = truncate ? "wb" : "ab";

    debug("Comm worker #%u: commit to file %s\n", header->id, filename);
    ERR((f = fopen(filename, mode)) == NULL, strerror(errno));

    // commit store
    uint32_t store_size = sizeof(nfl_entry_t) * header->max_n_entries;
    switch (compression_opt) {
    case COMPRESS_NONE:
        debug("Comm worker #%u: commit without compression\n", header->id);
        ret = nfl_commit_default(f, header, store, store_size);
        break;
    case COMPRESS_LZ4:
        debug("Comm worker #%u: commit with compression algorithm: lz4", header->id);
        ret = nfl_commit_lz4(f, header, store, store_size);
        break;
    case COMPRESS_ZSTD:
        debug("Comm worker #%u: commit with compression algorithm: zstd", header->id);
        ret = nfl_commit_zstd(f, header, store, store_size);
        break;
    // Must not reach here ...
    default:
        FATAL("Unknown compression option detected");
    }

    // Do fsync ?
    fclose(f);
    return ret;
}
