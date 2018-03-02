#include <errno.h>
#include <string.h>
#include "commit.h"

void nfl_commit_init() {

}

void nfl_commit_worker(nflog_header_t* header, nflog_entry_t* store, const char* filename) {
    FILE* f;
    uint32_t written;

    debug("Comm worker #%u: commit to file %s\n", header->id, filename);
    ERR((f = fopen(filename, "wb")) == NULL, strerror(errno));

    // commit header
    written = fwrite(header, 1, sizeof(nflog_header_t), f);
    ERR(written != sizeof(nflog_header_t), strerror(errno));

    // commit store
    uint32_t store_size = sizeof(nflog_entry_t) * header->max_n_entries;
    written = fwrite(store, 1, store_size, f);
    ERR(written != store_size, strerror(errno));

    // Do fsync ?
    fclose(f);
}
