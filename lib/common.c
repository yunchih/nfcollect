#include "common.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <regex.h>
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
    if (stat(storage_dir, &_d) != 0 || !S_ISDIR(_d.st_mode)) {
        return -1;
    }
    return 0;
}

int nfl_storage_match_index(const char *fn) {
    static regex_t regex;
    static bool compiled = false;
    regmatch_t match[1];
    int ret;

    if (!compiled) {
        ERR(regcomp(&regex, "^" STORAGE_PREFIX "_[0-9]+", 0),
            "Could not compile regex");
        compiled = true;
    }

    ret = regexec(&regex, fn, 1, match, 0);
    if (!ret) {
        assert(match[0].rm_so != (size_t)-1);
        return strtol(fn + match[0].rm_so, NULL, 10);
    } else if (ret != REG_NOMATCH) {
        char buf[100];
        regerror(ret, &regex, buf, sizeof(buf));
        WARN(1, "Regex match failed: %s", buf)
    }

    return -1;
}
const char *nfl_get_filename(const char *dir, int id) {
    char out[1024];
    sprintf(out, "%s/" STORAGE_PREFIX "_%d", dir, id);
    return strdup(out);
}

uint32_t nfl_get_filesize(FILE *f) {
    uint32_t size, prepos;
    prepos = ftell(f);
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, prepos, SEEK_SET);
    return size;
}

uint32_t nfl_header_cksum(nflog_header_t *header) {
    register uint64_t s = 3784672181;
    s += header->id;
    s ^= header->max_n_entries;
    s += header->n_entries;
    s ^= header->start_time;
    s += header->end_time;
    s &= UINT_MAX;
    return s;
}

void nfl_cal_trunk(uint32_t total_size, uint32_t *trunk_cnt,
                   uint32_t *trunk_size) {
    uint32_t pgsize = getpagesize();
    total_size *= 1024 * 1024; // MiB

    assert(trunk_cnt);
    assert(total_size);

    *trunk_cnt = CEIL_DIV(total_size, pgsize * TRUNK_SIZE_BY_PAGE);
    if (*trunk_cnt > MAX_TRUNK_ID) {
        *trunk_cnt = MAX_TRUNK_ID;
        *trunk_size = total_size / MAX_TRUNK_ID;
        *trunk_size = (*trunk_size / pgsize) * pgsize; // align with pagesize
    } else {
        *trunk_size = pgsize * TRUNK_SIZE_BY_PAGE;
    }
}

void nfl_cal_entries(uint32_t trunk_size, uint32_t *entries_cnt) {
    assert(entries_cnt);
    *entries_cnt =
        (trunk_size - sizeof(nflog_header_t)) / sizeof(nflog_entry_t);
}

void nfl_format_output(char *output, nflog_entry_t *entry) {
    char dest_ip[16];
    snprintf(dest_ip, 16, "%pI4", &entry->daddr);
    sprintf(output, "t=%ld\t"
                    "daddr=%s\t"
                    "proto=%s\t"
                    "uid=%d\t"
                    "sport=%d\t"
                    "dport=%d",
            entry->timestamp, dest_ip,
            entry->protocol == IPPROTO_TCP ? "TCP" : "UDP", entry->uid,
            entry->sport, entry->dport);
}

int nfl_setup_compression(const char *flag, enum nflog_compression_t *opt) {
    if (flag == NULL) {
        *opt = COMPRESS_NONE;
    } else if (!strcmp(flag, "zstd") || !strcmp(flag, "zstandard")) {
        *opt = COMPRESS_ZSTD;
    } else if (!strcmp(flag, "lz4")) {
        *opt = COMPRESS_LZ4;
    } else {
        fprintf(stderr, "Unknown compression algorithm: %s\n", flag);
        return 0;
    }

    return 1;
}
