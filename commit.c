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
    uint32_t id = header->id;

    sprintf(filename, "%s/%s_%d", storage_dir, storage_prefix, id);
    debug("Comm worker #%u: commit to file %s\n", header->id, filename);
    fd = open
    ERR((f = fopen(filename, "wb")) == NULL, strerror(errno));
    fwrite(header, sizeof(nflog_header_t), 1, f);

    uint32_t total_size = sizeof(nflog_entry_t) * header->max_n_entries;
    uint32_t total_blk = total_size / write_blk_size;
    uint32_t i, written = 0;
    for(i = 0; i < total_blk; ++i) {
        written = fwrite(store, 1, write_blk_size, f);

        while(written < write_blk_size) {
            written += fwrite(store, 1, write_blk_size - written, f);
        }
    }
    
    int remain = total_size - total_blk*write_blk_size;
    while(remain > 0) {
        remain -= fwrite(store, 1, remain, f);
    }

    fclose(f);
}

