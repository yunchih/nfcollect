
#include "extract.h"
#include <errno.h>
#include <string.h>
#include <time.h>
#define ZSTD_STATIC_LINKING_ONLY // ZSTD_findDecompressedSize
#include <zstd.h>

static int nfl_extract_default(FILE *f, nfl_state_t *state);
static int nfl_extract_zstd(FILE *f, nfl_state_t *state);
static int nfl_extract_lz4(FILE *f, nfl_state_t *state);

static int nfl_verify_header(nfl_header_t *header) {
    if (header->cksum != nfl_header_cksum(header)) {
        debug("Header checksum mismatch: expected: 0x%x, got: 0x%x",
                header->cksum, nfl_header_cksum(header));
        return -1;
    }

    if (header->id > MAX_TRUNK_ID)
        return -1;

    if (header->max_n_entries < header->n_entries)
        return -1;

    time_t now = time(NULL);
    if ((time_t)header->start_time >= now || (time_t)header->end_time >= now ||
        header->start_time > header->end_time)
        return -1;

    return 0;
}

static int nfl_extract_default(FILE *f, nfl_state_t *state) {
    int entries =
        fread(state->store, sizeof(nfl_entry_t), state->header->n_entries, f);
    WARN_RETURN(ferror(f), "%s", strerror(errno));
    return entries;
}

static int nfl_extract_zstd(FILE *f, nfl_state_t *state) {
    char *buf;
    size_t const compressed_size = nfl_get_filesize(f) - sizeof(nfl_header_t),
                 expected_decom_size =
                     state->header->n_entries * sizeof(nfl_entry_t);

    // It's possible that data or header is not written due to broken commit
    WARN_RETURN(compressed_size <= 0, "%s", "zstd: no data in this trunk");

    WARN_RETURN(!(buf = malloc(compressed_size)), "zstd: cannot malloc");
    WARN_RETURN(!fread(buf, compressed_size, 1, f), "zstd: broken data section");
    WARN_RETURN(ferror(f), "%s", strerror(errno));

    size_t const estimate_decom_size =
        ZSTD_findDecompressedSize(buf, compressed_size);
    if (estimate_decom_size == ZSTD_CONTENTSIZE_ERROR)
        FATAL("zstd: file was not compressed by zstd.\n");
    else if (estimate_decom_size == ZSTD_CONTENTSIZE_UNKNOWN)
        FATAL(
            "zstd: original size unknown. Use streaming decompression instead");

    size_t const actual_decom_size = ZSTD_decompress(
        state->store, expected_decom_size, buf, compressed_size);

    if (actual_decom_size != expected_decom_size) {
        FATAL("zstd: error decoding current file: %s \n",
              ZSTD_getErrorName(actual_decom_size));
    }

    free(buf);
    return actual_decom_size / sizeof(nfl_entry_t);
}

static int nfl_extract_lz4(FILE *f, nfl_state_t *state) {
    /* TODO */
    return 0;
}

int nfl_extract_worker(const char *filename, nfl_state_t *state, const time_range_t *range) {
    FILE *f;
    int got = 0, ret = 0;
    nfl_header_t *h;

    debug("Extracting from file %s", filename);
    ERR((f = fopen(filename, "rb")) == NULL, "extract worker");
    ERR(nfl_check_file(f) < 0, "extract worker");

    // Read header
    ERR(!(state->header = malloc(sizeof(nfl_header_t))),
        "extract malloc header");
    got = fread(state->header, sizeof(nfl_header_t), 1, f);
    h = state->header;

    if(h->end_time < range->from || h->start_time > range->until)
        return 0;

    // Check header validity
    WARN_RETURN(ferror(f), "%s", strerror(errno));
    WARN_RETURN(got == 0 || nfl_verify_header(h) < 0,
                "File %s has corrupted header.", filename);

    // Read body
    ERR(!(state->store = malloc(sizeof(nfl_entry_t) * h->n_entries)),
        "extract malloc store");
    switch (h->compression_opt) {
    case COMPRESS_NONE:
        debug("Extract worker #%u: extract without compression\n", h->id);
        ret = nfl_extract_default(f, state);
        break;
    case COMPRESS_LZ4:
        debug("Extract worker #%u: extract with compression algorithm: lz4",
              h->id);
        ret = nfl_extract_lz4(f, state);
        break;
    case COMPRESS_ZSTD:
        debug("Extract worker #%u: extract with compression algorithm: zstd",
              h->id);
        ret = nfl_extract_zstd(f, state);
        break;
    // Must not reach here ...
    default:
        FATAL("Unknown compression option detected");
    }

    fclose(f);

    return ret;
}
