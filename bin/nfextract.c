
// The MIT License (MIT)

// Copyright (c) 2018 Yun-Chih Chen

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in
// all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#define _XOPEN_SOURCE 700 // strptime
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809 // strdup
#endif

#include "extract.h"
#include "main.h"
#include "sql.h"
#include "util.h"

#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define PROG "nfextract"
#define DATE_FORMAT_HUMAN "YYYY-MM-DD [HH:MM][:SS]"
#define DATE_FORMAT "%Y-%m-%d"
#define DATE_FORMAT_FULL DATE_FORMAT " %H:%M"
#define DATE_FORMAT_FULL2 DATE_FORMAT " %H:%M:%S"
#define DATE_FORMAT_OUTPUT DATE_FORMAT_FULL2

const char *help_text =
    "Usage: " PROG " [OPTION]\n"
    "\n"
    "Options:\n"
    "  -d --storage=<dirname>     sqlite storage file\n"
    "  -h --help                  print this help\n"
    "  -v --version               print version information\n"
    "  -s --since=<date>          start showing entries on or newer than the "
    "specified date (format: " DATE_FORMAT_HUMAN ")\n"
    "  -u --until=<date>          stop showing entries on or older than the "
    "specified date (format: " DATE_FORMAT_HUMAN ")\n"
    "\n";

void sig_handler(int signo) {
    if (signo == SIGHUP)
        puts("Terminated due to SIGHUP ...");
}

static void callback(const State *s, const Timerange *range) {
    int nr_entries = s->header->nr_entries;

    DEBUG("callback: extracting %d entries", nr_entries);

    int i = 0;
    while (i < nr_entries && s->store[i].timestamp < range->from)
        i++;

    time_t last_t = 0;
    char timestamp[20];
    while (i < nr_entries && s->store[i].timestamp < range->until) {
        if (last_t != s->store[i].timestamp || !last_t) {
            last_t = s->store[i].timestamp;
            strftime(timestamp, 20, DATE_FORMAT_OUTPUT, localtime(&last_t));
        }

        printf("  "
               "%-18s:\t"
               "daddr=%-16s\t"
               "proto=%s\t"
               "uid=%d\t"
               "sport=%d\t"
               "dport=%d\n",
               timestamp, inet_ntoa(s->store[i].daddr),
               s->store[i].protocol == IPPROTO_TCP ? "TCP" : "UDP",
               s->store[i].uid, s->store[i].sport, s->store[i].dport);
        ++i;
    }
}

static void extract_all(const char *storage, const Timerange *range) {
    sqlite3 *db = NULL;
    db_open(&db, storage);
    db_read_data_by_timerange(db, range, callback);
    db_close(db);
}

static time_t parse_date_string(time_t default_t, const char *date) {
    struct tm parsed;
    char *ret;
    if (!date)
        return default_t;

#define PARSE(FORMAT)                                                          \
    ret = strptime(date, FORMAT, &parsed);                                     \
    if (ret && !*ret)                                                          \
        return mktime(&parsed);

    PARSE(DATE_FORMAT);
    PARSE(DATE_FORMAT_FULL);
    PARSE(DATE_FORMAT_FULL2);

    FATAL("Wrong date format: expected: \"" DATE_FORMAT_HUMAN "\", got: \"%s\"",
          date);
    return -1;
}

static void populate_date_range(Timerange *range, const char *since,
                                const char *until) {
    range->from = parse_date_string(0, since);
    range->until = parse_date_string(time(NULL), until);
}

int main(int argc, char *argv[]) {
    char *storage = NULL;
    char *date_since_str = NULL, *date_until_str = NULL;
    Timerange date_range;

    struct option longopts[] = {{"storage_file", required_argument, NULL, 'd'},
                                {"since", optional_argument, NULL, 's'},
                                {"until", optional_argument, NULL, 'u'},
                                {"help", no_argument, NULL, 'h'},
                                {"version", no_argument, NULL, 'v'},
                                {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "d:s:u:hv", longopts, NULL)) != -1) {
        switch (opt) {
        case 'h':
            printf("%s", help_text);
            exit(0);
            break;
        case 'v':
            printf("%s %s", PROG, VERSION);
            exit(0);
            break;
        case 'd':
            if (!optarg)
                FATAL("Expected: --storage_file=[PATH]");
            storage = strdup(optarg);
            break;
        case 's':
            if (!optarg)
                FATAL("Expected: --since=\"" DATE_FORMAT_HUMAN "\"");
            date_since_str = strdup(optarg);
            break;
        case 'u':
            if (!optarg)
                FATAL("Expected: --until=\"" DATE_FORMAT_HUMAN "\"");
            date_until_str = strdup(optarg);
            break;
        case '?':
            FATAL("Unknown argument, see --help");
        }
    }

    // verify arguments
    ASSERT(storage != NULL,
           "You must provide a storage directory (see --help)");

    if (check_file_exist(storage) < 0)
        ERROR("storage file not exist");

    if (signal(SIGHUP, sig_handler) == SIG_ERR)
        ERROR("Could not set SIGHUP handler");

    populate_date_range(&date_range, date_since_str, date_until_str);
    free(date_since_str);
    free(date_until_str);

    extract_all(storage, &date_range);
    free(storage);

    return 0;
}
