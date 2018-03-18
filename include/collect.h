#pragma once

#include "main.h"
void *nfl_collect_worker(void *targs);
void nfl_state_init(nfl_state_t **nf, uint32_t id, uint32_t entries_max,
                    nfl_global_t *g);
void nfl_state_free(nfl_state_t *nf);
