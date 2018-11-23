
// The MIT License (MIT)

// Copyright (c) 2017 Yun-Chih Chen
// Copyright (c) 2013 Florian Richter (nflogtable)

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

#include "collect.h"
#include "commit.h"
#include "common.h"
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>

const char *help_text =
    "Usage: " PACKAGE " [OPTION]\n"
    "\n"
    "Options:\n"
    "  -c --compression=<algo>      compression algorithm to use (default: no "
    "compression)\n"
    "  -d --storage_dir=<dirname>   log files storage directory\n"
    "  -h --help                    print this help\n"
    "  -g --nflog-group=<id>        the group id to collect\n"
    "  -p --parallelism=<num>       max number of committer thread\n"
    "  -t --truncate                whether or not to truncate existing trunks"
    " (default: no)\n"
    "  -s --storage_size=<dirsize>  log files maximum total size in MiB\n"
    "  -v --version                 print version information\n"
    "\n";

static void traverse_storage_dir(const char *storage_dir, uint32_t *starting_trunk, uint32_t *storage_size);
static nfl_nl_t netlink_fd;

static void sig_handler(int signo) {
    if (signo == SIGHUP) {
        puts("Terminated due to SIGHUP ...");
        nfl_close_netlink_fd(&netlink_fd);
    }
}

int main(int argc, char *argv[]) {
    uint32_t max_commit_worker = 0, storage_size = 0;
    uint32_t trunk_cnt = 0, trunk_size = 0;
    uint32_t entries_max, cur_trunk;
    bool truncate_trunks = false;

    nfl_global_t g;
    int nfl_group_id = -1;
    char *compression_flag = NULL, *storage_dir = NULL;

    struct option longopts[] = {/* name, has_args, flag, val */
                                {"nflog-group", required_argument, NULL, 'g'},
                                {"storage_dir", required_argument, NULL, 'd'},
                                {"storage_size", required_argument, NULL, 's'},
                                {"compression", optional_argument, NULL, 'z'},
                                {"parallelism", optional_argument, NULL, 'p'},
                                {"truncate", no_argument, NULL, 't'},
                                {"help", no_argument, NULL, 'h'},
                                {"version", no_argument, NULL, 'v'},
                                {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "c:g:d:s:hvp:", longopts, NULL)) !=
           -1) {
        switch (opt) {
        case 'h':
            printf("%s", help_text);
            exit(0);
            break;
        case 'v':
            printf("%s %s", PACKAGE, VERSION);
            exit(0);
            break;
        case 't':
            truncate_trunks = true;
            break;
        case 'c':
            compression_flag = optarg;
            break;
        case 'd':
            storage_dir = strdup(optarg);
            break;
        case 'g':
            nfl_group_id = atoi(optarg);
            break;
        case 'p':
            max_commit_worker = atoi(optarg);
            break;
        case 's':
            storage_size = atoi(optarg);
            break;
        case '?':
            fprintf(stderr, "Unknown argument, see --help\n");
            exit(1);
        }
    }

    // verify arguments
    ASSERT(nfl_group_id != -1,
           "You must provide a nflog group (see --help)!\n");
    ASSERT(storage_dir != NULL,
           "You must provide a storage directory (see --help)\n");
    ASSERT(storage_size != 0, "You must provide the desired size of log file "
                              "(in MiB) (see --help)\n");

    ERR(nfl_check_dir(storage_dir) < 0, "storage directory not exist");

    // max number of commit worker defaults to #processor - 1
    if (max_commit_worker == 0) {
        max_commit_worker = sysconf(_SC_NPROCESSORS_ONLN) - 1;
        max_commit_worker = max_commit_worker > 0 ? max_commit_worker : 1;
    }

    g.storage_dir = storage_dir;

    // register signal handler
    ERR(signal(SIGHUP, sig_handler) == SIG_ERR, "Could not set SIGHUP handler");

    nfl_cal_trunk(storage_size, &trunk_cnt, &trunk_size);
    nfl_cal_entries(trunk_size, &entries_max);
    nfl_setup_compression(compression_flag, &g.compression_opt);

    // Set up commit worker
    g.nfl_commit_queue = malloc(sizeof(sem_t));
    sem_init(g.nfl_commit_queue, 0, max_commit_worker);

    // Calculate storage consumed
    pthread_mutex_init(&g.nfl_storage_consumed_lock, NULL);
    g.nfl_storage_consumed = 0;

    // Set up nflog receiver worker
    nfl_state_t **trunks = (nfl_state_t **)calloc(trunk_cnt, sizeof(void *));

    info(PACKAGE ": storing in directory '%s', capped by %d MiB", storage_dir,
         storage_size);
    info(PACKAGE ": workers started, entries per trunk = %d, #trunks = %d",
         entries_max, trunk_cnt);

    calculate_starting_trunk(storage_dir, &cur_trunk, &g.nfl_storage_consumed);
    if (truncate_trunks) {
        cur_trunk = 0;
        info(PACKAGE ": requested to truncate (overwrite) trunks in %s",
             storage_dir);
    } else {
        cur_trunk = cur_trunk < 0 ? 0: NEXT(cur_trunk, trunk_cnt);
        const char *fn = nfl_get_filename(storage_dir, cur_trunk);
        info(PACKAGE ": will start writing to trunk %s and onward", fn);
        free((char *)fn);
    }

    nfl_open_netlink_fd(&netlink_fd, nfl_group_id);
    for (;; cur_trunk = NEXT(cur_trunk, trunk_cnt)) {
        debug("Running receiver worker: id = %d", cur_trunk);
        nfl_state_init(&(trunks[cur_trunk]), cur_trunk, entries_max, &g);
        trunks[cur_trunk]->netlink_fd = &netlink_fd;

        pthread_create(&(trunks[cur_trunk]->thread), NULL, nfl_collect_worker,
                       (void *)trunks[cur_trunk]);
        // wait for current receiver worker
        pthread_join(trunks[cur_trunk]->thread, NULL);
    }

    // Won't reach here
    // We don't actually free trunks or the semaphore at all
    sem_destroy(g.nfl_commit_queue);
    nfl_close_netlink_fd(&netlink_fd);
    xit(0);
    uint32_t start_trunk;
}

/*
 * traverse_storage_dir does 2 things:
 * 1. Find starting trunk
 *   Find the trunk to start with after a restart
 *   We choose the one with newest modification time.
 *   If no existing trunk is found, set to -1
 * 2. Sum storage size consumed by adding up stored sizes.
 */
static void traverse_storage_dir(const char *storage_dir, uint32_t *starting_trunk, uint32_t *storage_size) {
    DIR *dp;
    struct stat stat;
    struct dirent *ep;
    time_t newest = (time_t)0;
    uint32_t newest_index = -1, _storage_size;
    int index;
    char cwd[100];

    ERR(!(dp = opendir(storage_dir)), "Can't open the storage directory");

    ERR(!getcwd(cwd, sizeof(cwd)), "getcwd");
    ERR(chdir(storage_dir) < 0, "chdir");

    while ((ep = readdir(dp))) {
        const char *fn = ep->d_name;
        index = nfl_storage_match_index(fn);
        if (index >= 0 && index < MAX_TRUNK_ID) {
            ERR(lstat(fn, &stat) < 0, fn);
            if (difftime(stat.st_mtime, newest) > 0) {
                newest = stat.st_mtime;
                _storage_size = (uint32_t)index;
            }

            *storage_size += stat.st_size
        }
    }

    closedir(dp);
    ERR(chdir(cwd) < 0, "chdir");
    *starting_trunk = newest_index;
    *storage_size = _storage_size;
}
