
#include "extract.h"
#include <errno.h>
#include <string.h>
#include <time.h>

static int nfl_extract_default(FILE *f, nflog_state_t *state);
static int nfl_extract_zstd(FILE *f, nflog_state_t *state);
static int nfl_extract_lz4(FILE *f, nflog_state_t *state);

typedef int (*nflog_extract_run_table_t)(FILE *f, nflog_state_t *state);
static const nflog_extract_run_table_t extract_run_table[] = {
    nfl_extract_default, nfl_extract_lz4, nfl_extract_zstd};

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
    /* TODO */
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
    ERR((*header = malloc(sizeof(nflog_header_t))), NULL);
    got = fread(*header, 1, sizeof(nflog_header_t), f);

    // Check header validity
    WARN_RETURN(ferror(f), "%s", strerror(errno));
    WARN_RETURN(got != sizeof(nflog_header_t) || nfl_verify_header(*header) < 0,
                "File %s has corrupted header.", filename);

    // Read body
    WARN_RETURN((*header)->compression_opt >
                    sizeof(extract_run_table) / sizeof(extract_run_table[0]),
                "Unknown compression in %s", filename);
    ERR((*store = malloc(sizeof(nflog_entry_t) * (*header)->n_entries)), NULL);
    ret = extract_run_table[(*header)->compression_opt](f, state);
    fclose(f);

    return ret;
}
