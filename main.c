
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

#include "commit.h"
#include "main.h"
#include "nflog.h"
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

sem_t nfl_commit_queue;
uint16_t nfl_group_id;
char *storage_dir = NULL;
const char *storage_prefix = "nflog_storage";

const char *version_text = "nfcollect Version 0.1\n";
const char *help_text =
    "Usage: nfcollect [OPTION]\n"
    "Foo bar\n"
    "\n"
    "Options:\n"
    "  -h --help                 print this help\n"
    "  -v --version              print version information\n"
    "     --nflog-group=<id>     nflog group\n"
    "\n";

void sig_handler(int signo) {
    if (signo == SIGHUP) {
        /* TODO */
    }
}
nflog_state_t *get_nflog_state(uint32_t id, uint32_t entries_max) {
    nflog_state_t *state =
        (nflog_state_t *)malloc(sizeof(nflog_state_t));
    pthread_mutex_init(&(state->lock), NULL);
    state->store = (nflog_entry_t *)malloc(sizeof(nflog_entry_t) *
                                                  entries_max);
    state->header.id = id;
    state->header.max_n_entries = entries_max;
    state->header.n_entries = 0;
    return state;
}

void free_nflog_state(nflog_state_t **state) { *state = NULL; }

int main(int argc, char *argv[]) {

    uint32_t i, max_commit_worker = 0, storage_size = 0;
    int nflog_group_id;

    struct option longopts[] = {/* name, has_args, flag, val */
                                {"nflog-group", required_argument, NULL, 'g'},
                                {"storage_dir", required_argument, NULL, 'd'},
                                {"storage_size", required_argument, NULL, 's'},
                                {"help", no_argument, NULL, 'h'},
                                {"version", no_argument, NULL, 'v'},
                                {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "g:d:hv", longopts, NULL)) != -1) {
        switch (opt) {
        case 'h':
            printf("%s", help_text);
            exit(0);
            break;
        case 'v':
            printf("%s", version_text);
            exit(0);
            break;
        case 'f':
            storage_dir = optarg;
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
    ASSERT(storage_dir != NULL,
           "You must provide a storage directory (see --help)\n");
    ASSERT(storage_size == 0, "You must provide the desired size of log file "
                              "(in MiB) (see --help)\n");
    struct stat _d;
    if(stat(storage_dir, &_d) != 0 || !S_ISDIR(_d.st_mode)){
        fprintf(stderr, "storage directory '%s' not exist", storage_dir);
    }

    // max number of commit worker defaults to #processor - 1
    if (max_commit_worker == 0) {
        max_commit_worker = sysconf(_SC_NPROCESSORS_ONLN) - 1;
        max_commit_worker = max_commit_worker > 0 ? max_commit_worker : 1;
    }

    nfl_group_id = nflog_group_id;

    // register signal handler
    ERR(signal(SIGHUP, sig_handler) == SIG_ERR, "Could not set SIGHUP handler");

    uint32_t pgsize = getpagesize();
    uint32_t trunk_size_byte = storage_size / TRUNK_SIZE * 1024 * 1024; // MiB
    trunk_size_byte = (trunk_size_byte / pgsize) * pgsize; // align with pagesize

    uint32_t trunk_cnt = CEILING(storage_size, trunk_size_byte);
    uint32_t entries_max = (trunk_size_byte - sizeof(nflog_header_t)) /
                           sizeof(nflog_entry_t);

    // Set up commit worker
    sem_init(&nfl_commit_queue, 0, max_commit_worker);

    // Set up nflog receiver worker
    nflog_state_t **trunks = (nflog_state_t **)malloc(
        sizeof(nflog_state_t *) * trunk_cnt);
    for (i = 0; i < trunk_cnt; ++i) {
        trunks[i] = NULL;
    }
    
    nfl_commit_init(trunk_cnt);

    for (i = 0;; i = (i + 1) % trunk_cnt) {
        trunks[i] =
            trunks[i] != NULL ? trunks[i] : get_nflog_state(i, entries_max);
        pthread_mutex_lock(&(trunks[i]->lock));
        pthread_create(&(trunks[i]->thread), NULL, nflog_worker,
                       (void *)trunks[i]);
        pthread_join(trunks[i]->thread, NULL);
    }
    
    sem_destroy(&nfl_commit_queue);
    exit(0);
}
