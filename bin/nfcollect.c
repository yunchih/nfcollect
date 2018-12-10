
// The MIT License (MIT)

// Copyright (c) 2018 Yun-Chih Chen
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
#include <unistd.h>

const char *help_text =
    "Usage: " PACKAGE " [OPTION]\n"
    "\n"
    "Options:\n"
    "  -c --compression=<algo>      compression algorithm to use (default: no "
    "compression)\n"
    "  -d --storage=<filename>         sqlite database storage file\n"
    "  -h --help                       print this help\n"
    "  -g --nflog_group=<id>           the group id to collect\n"
    "  -s --storage_size=<max DB size> maximum DB size in MiB\n"
    "  -V --vacuum                     vacuum the database on startup\n"
    "  -v --version                    print version information\n"
    "\n";

static Netlink netlink_fd;
static void sig_handler(int signo) {
    if (signo == SIGHUP) {
        puts("Terminated due to SIGHUP ...");
        collect_close_netlink(&netlink_fd);
    }
}

int main(int argc, char *argv[]) {
    uint32_t storage_size = 0;
    Global g;
    int nflog_group_id = -1;
    char *compression_flag = NULL, *storage = NULL;
    bool do_vacuum = false;

    struct option longopts[] = {/* name, has_args, flag, val */
                                {"nflog_group", required_argument, NULL, 'g'},
                                {"storage", required_argument, NULL, 'd'},
                                {"storage_size", required_argument, NULL, 's'},
                                {"compression", optional_argument, NULL, 'z'},
                                {"vacuum", optional_argument, NULL, 'V'},
                                {"help", no_argument, NULL, 'h'},
                                {"version", no_argument, NULL, 'v'},
                                {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "c:g:d:s:hVvp:", longopts, NULL)) !=
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
        case 'c':
            compression_flag = optarg;
            break;
        case 'd':
            storage = strdup(optarg);
            break;
        case 'g':
            nflog_group_id = atoi(optarg);
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
    ASSERT(nflog_group_id != -1,
           "You must provide a nflog group (see --help)!\n");
    ASSERT(storage != NULL, "You must provide a storage file (see --help)\n");
    ASSERT(storage_size != 0, "You must provide the desired size of log file "
                              "(in MiB) (see --help)\n");

    g.compression_type = get_compression(compression_flag);
    if (check_basedir_exist(storage) < 0)
        FATAL("Storage directory does not exist");

    // register signal handler
    if (signal(SIGHUP, sig_handler) == SIG_ERR)
        ERROR("Could not set SIGHUP handler");

    pthread_mutex_init(&g.storage_consumed_lock, NULL);
    g.storage_budget = storage_size * 1024 * 1024; // MB
    g.storage_consumed = 0;
    g.storage_file = (const char *)storage;
    g.max_nr_entries = g_max_nr_entries_default;

    collect_open_netlink(&netlink_fd, nflog_group_id);

    pthread_t worker;
    State *state;
    INFO(PACKAGE
         ": storing in file '%s' (current size: %.2f MB), capped by %d MiB",
         g.storage_file, (float)g.storage_consumed / 1024.0 / 1024.0,
         storage_size);
    INFO(PACKAGE ": workers started, entries per block = %d", g.max_nr_entries);

    while (true) {
        state_init(&state, &netlink_fd, &g);
        pthread_create(&worker, NULL, collect_worker, (void *)state);
        pthread_join(worker, NULL);
    }

    collect_close_netlink(&netlink_fd);
}
