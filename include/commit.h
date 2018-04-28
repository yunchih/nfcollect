#ifndef _COMMIT_H
#define _COMMIT_H

#include "common.h"
void nfl_commit_init();
void nfl_commit_worker(nfl_header_t *header, nfl_entry_t *store,
                       enum nfl_compression_t compression_opt,
                       bool truncate,
                       const char *filename);

#endif
