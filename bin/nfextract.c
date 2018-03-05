
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

#include "extract.h"
#include "common.h"
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

#define PROG "nfextract"

sem_t nfl_commit_queue;
uint16_t nfl_group_id;

const char *help_text =
    "Usage: " PROG " [OPTION]\n"
    "\n"
    "Options:\n"
    "  -d --storage_dir=<dirname>   log files storage directory\n"
    "  -h --help                    print this help\n"
    "  -v --version                 print version information\n"
    "\n";

static void sig_handler(int signo) {
    if (signo == SIGHUP) {
        /* TODO */
    }
}

static void extract_each(const char *filename) {
    nflog_state_t trunk;
    if(nfl_extract_worker(filename, &trunk) < 0)
        return;

    char output[1024];
    for(int entry = 0; entry < trunk.header->n_entries; ++entry){
        nfl_format_output(output, trunk.store);
        puts((char*)output);
        free((char*)output);
    }

    free((char*)filename);
}

static void extract_all(const char *storage_dir) {
	DIR *dp;
	struct dirent *ep;
    int i, index, max_index = -1;
    char *trunk_files[MAX_TRUNK_ID];
    memset(trunk_files, MAX_TRUNK_ID, 0);

    ERR(!(dp = opendir(storage_dir)),
            "Can't open the storage directory");
    while ((ep = readdir(dp))) {
        index = nfl_storage_match_index(ep->d_name);
        if(index >= 0) {
            if(index >= MAX_TRUNK_ID) {
                WARN(1, "Storage trunk file index "
                        "out of predefined range: %s", ep->d_name);
            } else {
                trunk_files[index] = strdup(ep->d_name);
                if(index > max_index) max_index = index;
            }
        }
    }

    closedir (dp);

    for(i = 0; i < max_index; ++i)
        if(trunk_files[i])
            extract_each(trunk_files[i]);
}

int main(int argc, char *argv[]) {
    char *storage_dir = NULL;
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

    extract_all(storage_dir);
    return 0;
}
