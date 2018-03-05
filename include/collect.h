#pragma once

void *nfl_collect_worker(void *targs);
void nfl_state_init(nflog_state_t **nf, uint32_t id, uint32_t entries_max,
                    nflog_global_t *g);
void nfl_state_free(nflog_state_t *nf);
