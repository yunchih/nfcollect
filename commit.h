#pragma once
#include "main.h"

void nfl_commit_init();
void nfl_commit_worker(nflog_header_t* header, nflog_entry_t* store);
