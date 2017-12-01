#pragma once

void* nflog_worker(void *targs);
void nfl_state_update_or_create(nflog_state_t **nf, uint32_t id, uint32_t entries_max);
void nfl_state_free(nflog_state_t *nf);
