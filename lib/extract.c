#include "main.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define ZSTD_STATIC_LINKING_ONLY // ZSTD_findDecompressedSize
#include <zstd.h>

static bool extract_default(State *s, const void *src) {
    s->store = malloc(s->header->raw_size);
    memcpy(s->store, src, s->header->raw_size);
    return true;
}

static bool extract_zstd(State *s, const void *src) {
    assert(src);
    size_t const expected_decom_size = s->header->nr_entries * sizeof(Entry);

    size_t const r = ZSTD_findDecompressedSize(src, s->header->raw_size);
    if (r == ZSTD_CONTENTSIZE_ERROR)
        FATAL("zstd: file was not compressed by zstd.\n");
    else if (r == ZSTD_CONTENTSIZE_UNKNOWN)
        FATAL(
            "zstd: original size unknown. Use streaming decompression instead");

    if (r != expected_decom_size) {
        WARN("zstd: expected decompressed size: %ld, got: %ld, skipping "
             "decompression",
             expected_decom_size, r);
        return false;
    }

    s->store = malloc(expected_decom_size);
    size_t const actual_decom_size = ZSTD_decompress(
        s->store, expected_decom_size, src, s->header->raw_size);

    if (actual_decom_size != expected_decom_size) {
        FATAL("zstd: error decoding current file: %s \n",
              ZSTD_getErrorName(actual_decom_size));
    }

    return true;
}

static bool extract_lz4(State *s, const void *src) {
    // TODO
    (void)s; (void)src;
    return true;
}

bool extract(State *s, const void *src) {
    switch (s->header->compression_type) {
    case COMPRESS_NONE:
        DEBUG("extract: extract without compression\n");
        return extract_default(s, src);
    case COMPRESS_LZ4:
        DEBUG("extract: extract with compression algorithm: lz4");
        return extract_lz4(s, src);
    case COMPRESS_ZSTD:
        DEBUG("extract: extract with compression algorithm: zstd");
        return extract_zstd(s, src);
    // Must not reach here ...
    default:
        FATAL("Unknown compression option detected");
    }
}
