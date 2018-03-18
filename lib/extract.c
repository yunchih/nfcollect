
#include "extract.h"
#include <errno.h>
#include <string.h>
#include <time.h>
#define ZSTD_STATIC_LINKING_ONLY // ZSTD_findDecompressedSize
#include <zstd.h>

static int nfl_extract_default(FILE *f, nflog_state_t *state);
static int nfl_extract_zstd(FILE *f, nflog_state_t *state);
static int nfl_extract_lz4(FILE *f, nflog_state_t *state);

static int nfl_verify_header(nflog_header_t *header) {
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

static int nfl_extract_default(FILE *f, nflog_state_t *state) {
    fread(state->store, state->header->n_entries, sizeof(nflog_entry_t), f);
    WARN_RETURN(ferror(f), "%s", strerror(errno));
    return 0;
}

static int nfl_extract_zstd(FILE *f, nflog_state_t *state) {
    char *buf;
    size_t const compressed_size = nfl_get_filesize(f) - sizeof(nflog_header_t),
                 estimate_decom_size = ZSTD_findDecompressedSize(state->store, compressed_size),
                 expected_decom_size = state->header->n_entries * sizeof(nflog_entry_t);

    if (estimate_decom_size == ZSTD_CONTENTSIZE_ERROR)
        FATAL("zstd: file was not compressed by zstd.\n");
    else if (estimate_decom_size == ZSTD_CONTENTSIZE_UNKNOWN)
        FATAL("zstd: original size unknown. Use streaming decompression instead");

    ERR(!(buf = malloc(compressed_size)), "zstd: cannot malloc");
    fread(buf, compressed_size, 1, f);
    WARN_RETURN(ferror(f), "%s", strerror(errno));

    size_t const actual_decom_size =
        ZSTD_decompress(state->store, expected_decom_size, buf, compressed_size);

    if (actual_decom_size != expected_decom_size) {
        FATAL("zstd: error decoding current file: %s \n", ZSTD_getErrorName(actual_decom_size));
	}

    free(buf);
    return 0;
}

static int nfl_extract_lz4(FILE *f, nflog_state_t *state) {
    /* TODO */
    return 0;
}

int nfl_extract_worker(const char *filename, nflog_state_t *state) {
    FILE *f;
    int got = 0, ret = 0;
    nflog_header_t **header = &state->header;
    nflog_entry_t **store = &state->store;

    debug("Extracting from file %s", filename);
    ERR((f = fopen(filename, "rb")) == NULL, "extract worker");
    ERR(nfl_check_file(f) < 0, "extract worker");

    // Read header
    ERR(!(*header = malloc(sizeof(nflog_header_t))), NULL);
    got = fread(*header, 1, sizeof(nflog_header_t), f);

    // Check header validity
    WARN_RETURN(ferror(f), "%s", strerror(errno));
    WARN_RETURN(got != sizeof(nflog_header_t) || nfl_verify_header(*header) < 0,
                "File %s has corrupted header.", filename);

    // Read body
    ERR((*store = malloc(sizeof(nflog_entry_t) * (*header)->n_entries)), NULL);
    switch((*header)->compression_opt) {
        case COMPRESS_NONE:
            debug("Extract worker #%u: extract without compression\n", (*header)->id)
            nfl_extract_default(f, state);
            break;
        case COMPRESS_LZ4:
            debug("Extract worker #%u: extract with compression algorithm: lz4", (*header)->id)
            nfl_extract_lz4(f, state);
            break;
        case COMPRESS_ZSTD:
            debug("Extract worker #%u: extract with compression algorithm: zstd", (*header)->id)
            nfl_extract_zstd(f, state);
            break;
        // Must not reach here ...
        default: FATAL("Unknown compression option detected");
    }

    fclose(f);

    return ret;
}
