#ifndef _COMMIT_H
#define _COMMIT_H

#include "common.h"
void nfl_commit_init();
void nfl_commit_worker(nflog_header_t* header, nflog_entry_t* store, const char* filename);

#endif
