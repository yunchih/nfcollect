#pragma once

#include "main.h"
int nfl_check_file(FILE *f);
int nfl_check_dir(const char *storage_dir);
const char *nfl_get_filename(const char *dir, int id);
void nfl_cal_trunk(uint32_t total_size, uint32_t *trunk_cnt, uint32_t *trunk_size);
void nfl_cal_entries(uint32_t trunk_size, uint32_t *entries_cnt);
const char *nfl_format_output(nflog_entry_t *entry);
