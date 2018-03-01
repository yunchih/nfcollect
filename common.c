#include "common.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int nfl_check_file(FILE *f) {
	struct stat s;
	assert(f);
	if (fstat(fileno(f), &s) < 0)
		return -errno;

	// Ignore file already unlinked
	if (s.st_nlink <= 0)
		return -EIDRM;

	return 0;
}

int nfl_check_dir(const char *storage_dir) {
    struct stat _d;
    if(stat(storage_dir, &_d) != 0 || !S_ISDIR(_d.st_mode)){
        return -1;
    }
    return 0;
}

const char *nfl_get_filename(const char *dir, int id) {
    char out[1024];
    sprintf(out, "%s/" STORAGE_PREFIX "_%d", dir, id);
    return strdup(out);
}

uint32_t nfl_header_cksum(nflog_header_t *header) {
	register uint64_t s = 3784672181;
	s += header->id;
	s ^= header->max_n_entries;
	s += header->n_entries;
	s ^= header->start_time;
	s += header->end_time;
	s &= ULONG_MAX;
	return s;
}

void nfl_cal_trunk(uint32_t total_size, uint32_t *trunk_cnt, uint32_t *trunk_size) {
	uint32_t pgsize = getpagesize();
    total_size *= 1024 * 1024; // MiB

	assert(trunk_cnt);
	assert(total_size);

    *trunk_cnt = CEIL_DIV(total_size, pgsize * TRUNK_SIZE_BY_PAGE);
	if(*trunk_cnt > MAX_TRUNK_ID) {
		*trunk_size = total_size / MAX_TRUNK_ID;
		*trunk_size = (*trunk_size / pgsize) * pgsize; // align with pagesize
	}
	else {
		*trunk_size = TRUNK_SIZE_BY_PAGE;
	}
}

void nfl_cal_entries(uint32_t trunk_size, uint32_t *entries_cnt) {
	assert(entries_cnt);
    *entries_cnt = (trunk_size - sizeof(nflog_header_t)) / sizeof(nflog_entry_t);
}

const char *nfl_format_output(nflog_entry_t *entry) {
    char out[1024], dest_ip[16];
    snprintf(dest_ip, 16, "%pI4", &entry->daddr);
    sprintf(out,
          "t=%ld\t"
          "daddr=%s\t"
          "proto=%s\t"
          "uid=%d\t"
          "sport=%d\t"
          "dport=%d",
          entry->timestamp, dest_ip,
          entry->protocol == IPPROTO_TCP ? "TCP" : "UDP",
          entry->uid, entry->sport, entry->dport);
    return strdup(out);
}
