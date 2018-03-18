#pragma once

#include "main.h"
int nfl_check_file(FILE *f);
int nfl_check_dir(const char *storage_dir);
int nfl_storage_match_index(const char *fn);
const char *nfl_get_filename(const char *dir, int id);
uint32_t nfl_get_filesize(FILE *f);
uint32_t nfl_header_cksum(nfl_header_t *header);
void nfl_cal_trunk(uint32_t total_size, uint32_t *trunk_cnt,
                   uint32_t *trunk_size);
void nfl_cal_entries(uint32_t trunk_size, uint32_t *entries_cnt);
void nfl_format_output(char *output, nfl_entry_t *entry);
int nfl_setup_compression(const char *flag, enum nfl_compression_t *opt);
