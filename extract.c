
#include "common.h"
#include <errno.h>
#include <string.h>
#include <time.h>

static int nfl_verify_header(nflog_header_t *header) {
    if(header->id > MAX_TRUNK_ID)
        return -1;

    if(header->max_n_entries < header->n_entries)
        return -1;

    time_t now = time(NULL);
    if((time_t) header->start_time >= now ||
       (time_t) header->end_time >= now ||
       header->start_time > header->end_time)
        return -1;
    return 0;
}

int nfl_extract_worker(nflog_header_t *header, nflog_entry_t *store, const char *filename) {
    FILE* f;
    uint32_t got;
    int i, failed = 0;

    debug("Extracting from file %s", filename);
    ERR((f = fopen(filename, "rb")) == NULL, "extract worker");
    ERR(nfl_check_file(f) < 0, "extract worker");

    // Read header
    got = fread(header, 1, sizeof(nflog_header_t), f);

    // Check header validity
    WARN_RETURN(ferror(f), "%s", strerror(errno));
    WARN_RETURN(nfl_verify_header(header) < 0, "File %s has corrupted header.", filename);

    // Read body
    fread(store, header->n_entries, sizeof(nflog_entry_t), f);
    WARN_RETURN(ferror(f), "%s", strerror(errno));
    fclose(f);
}
