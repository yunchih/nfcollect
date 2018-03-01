
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

#include "commit.h"
#include "common.h"
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#define PROG "nfextract"

sem_t nfl_commit_queue;
uint16_t nfl_group_id;
char *storage_dir = NULL;

const char *help_text =
    "Usage: " PROG " [OPTION]\n"
    "\n"
    "Options:\n"
    "  -d --storage_dir=<dirname>   log files storage directory\n"
    "  -h --help                    print this help\n"
    "  -v --version                 print version information\n"
    "\n";

void sig_handler(int signo) {
    if (signo == SIGHUP) {
        /* TODO */
    }
}

int main(int argc, char *argv[]) {

    uint32_t i, max_commit_worker = 0, storage_size = 0;
    uint32_t trunk_cnt, trunk_size, entries_max;
    int nflog_group_id;

    struct option longopts[] = {/* name, has_args, flag, val */
                                {"storage_dir", required_argument, NULL, 'd'},
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
            storage_dir = optarg;
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

    nfl_cal_trunk(storage_size, &trunk_cnt, &trunk_size);
    nfl_cal_entries(trunk_size, &entries_max);

    nflog_state_t trunk;
    const char *filename, *output;
    for (int i = 0; i < trunk_cnt; ++i) {
        filename = nfl_get_filename(storage_dir, i);
        nfl_extract_worker(trunk.header, trunk.store, filename);

        for(int entry = 0; entry < trunk.header->n_entries; ++entry){
            output = nfl_format_output(trunk.store);
            puts((char*)output);
            free((char*)output);
        }

        free((char*)filename);
    }

    return 0;
}
