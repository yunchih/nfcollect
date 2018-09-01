
// The MIT License (MIT)

// Copyright (c) 2017 Yun-Chih Chen

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

#include "common.h"
#include "extract.h"
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
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PROG "nfextract"
#define DATE_FORMAT_HUMAN "YYYY-MM-DD [HH:MM][:SS]"
#define DATE_FORMAT "%Y-%m-%d"
#define DATE_FORMAT_FULL DATE_FORMAT " %H:%M"
#define DATE_FORMAT_FULL2 DATE_FORMAT " %H:%M:%S"

sem_t nfl_commit_queue;
uint16_t nfl_group_id;

const char *help_text =
    "Usage: " PROG " [OPTION]\n"
    "\n"
    "Options:\n"
    "  -d --storage_dir=<dirname> log files storage directory\n"
    "  -h --help                  print this help\n"
    "  -v --version               print version information\n"
    "  -s --since                 start showing entries on or newer than the specified date (format: " DATE_FORMAT_HUMAN ")\n"
    "  -u --until                 stop showing entries on or older than the specified date (format: " DATE_FORMAT_HUMAN ")\n"
    "\n";

void sig_handler(int signo) {
    if (signo == SIGHUP)
        puts("Terminated due to SIGHUP ...");
}

static void extract_each(const char *storage_dir, const char *filename, const time_range_t *range) {
    nfl_state_t trunk;

    // Build full path
    char *fullpath = malloc(strlen(storage_dir) + strlen(filename) + 2);
    sprintf(fullpath, "%s/%s", storage_dir, filename);

    debug("Extracting storage file: %s", fullpath);
    int entries = nfl_extract_worker(fullpath, &trunk, range);
    free(fullpath);

    int i = 0;
    while(i < entries && trunk.store[i].timestamp < range->from)
        i++;

    char output[1024];
    while(i < entries && trunk.store[i].timestamp < range->until) {
        nfl_format_output(output, &trunk.store[i]);
        puts((char *)output);
        ++i;
    }
}

static void extract_all(const char *storage_dir, const time_range_t *range) {
    DIR *dp;
    struct dirent *ep;
    int i, index, max_index = -1;
    char *trunk_files[MAX_TRUNK_ID];
    memset(trunk_files, 0, sizeof(trunk_files));

    ERR(!(dp = opendir(storage_dir)), "Can't open the storage directory");
    while ((ep = readdir(dp))) {
        index = nfl_storage_match_index(ep->d_name);
        if (index >= 0) {
            debug("Storage file %s matches with index %d", ep->d_name, index);
            if (index >= MAX_TRUNK_ID) {
                WARN(1, "Storage trunk file index "
                        "out of predefined range: %s",
                     ep->d_name);
                return;
            } else {
                trunk_files[index] = strdup(ep->d_name);
                if (index > max_index)
                    max_index = index;
            }
        }
    }

    closedir(dp);

    for (i = 0; i <= max_index; ++i) {
        if (trunk_files[i])
            extract_each(storage_dir, trunk_files[i], range);
        free(trunk_files[i]);
    }
}

static time_t parse_date_string(time_t default_t, const char *date) {
    struct tm parsed;
    char *ret;
    if(!date) return default_t;

#define PARSE(FORMAT) \
        ret = strptime(date, FORMAT, &parsed); \
        if(ret && !*ret) return mktime(&parsed); \

    PARSE(DATE_FORMAT);
    PARSE(DATE_FORMAT_FULL);
    PARSE(DATE_FORMAT_FULL2);

    FATAL("Wrong date format: expected: \"" DATE_FORMAT_HUMAN "\", got: \"%s\"", date);
    return -1;
}
static void populate_date_range(time_range_t *range, const char *since, const char *until) {
    range->from  = parse_date_string(0, since);
    range->until = parse_date_string(time(NULL), until);
}

int main(int argc, char *argv[]) {
    char *storage_dir = NULL;
    char *date_since_str = NULL, *date_until_str = NULL;
    time_range_t date_range;

    struct option longopts[] = {/* name, has_args, flag, val */
                                {"storage_dir", required_argument, NULL, 'd'},
                                {"since", optional_argument, NULL, 's'},
                                {"until", optional_argument, NULL, 'u'},
                                {"help", no_argument, NULL, 'h'},
                                {"version", no_argument, NULL, 'v'},
                                {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "d:hv", longopts, NULL)) != -1) {
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
            if(!optarg) FATAL("Expected: --storage_dir=[PATH]");
            storage_dir = strdup(optarg);
            break;
        case 's':
            if(!optarg) FATAL("Expected: --since=\"" DATE_FORMAT_HUMAN "\"");
            date_since_str = strdup(optarg);
            break;
        case 'u':
            if(!optarg) FATAL("Expected: --until=\"" DATE_FORMAT_HUMAN "\"");
            date_until_str = strdup(optarg);
            break;
        case '?':
            fprintf(stderr, "Unknown argument, see --help");
            exit(1);
        }
    }

    // verify arguments
    ASSERT(storage_dir != NULL,
           "You must provide a storage directory (see --help)");

    ERR(nfl_check_dir(storage_dir) < 0, "storage directory not exist");

    // register signal handler
    ERR(signal(SIGHUP, sig_handler) == SIG_ERR, "Could not set SIGHUP handler");

    populate_date_range(&date_range, date_since_str, date_until_str);
    free(date_since_str); free(date_until_str);

    extract_all(storage_dir, &date_range);
    free(storage_dir);

    return 0;
}
