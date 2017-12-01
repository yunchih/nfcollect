#include <errno.h>
#include <string.h>
#include "commit.h"

extern char *storage_dir;
extern char *storage_prefix;
const uint32_t write_blk_size = 8196;

void nfl_commit_init() {

}

void nfl_commit_worker(nflog_header_t* header, nflog_entry_t* store) {
    FILE* f;
    char filename[1024];
    uint32_t written, id = header->id;

    sprintf(filename, "%s/%s_%d", storage_dir, storage_prefix, id);
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

