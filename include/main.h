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

#ifndef _MAIN_H
#define _MAIN_H
#include <assert.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef DEBUG
#define DEBUG_ON 1
#else
#define DEBUG_ON 0
#endif

#define ASSERT(condition, error_msg)                                           \
    if (!(condition)) {                                                        \
        fputs((error_msg), stderr);                                            \
        exit(1);                                                               \
    }

#define ERR(command, error_msg)                                                \
    if (command) {                                                             \
        perror((error_msg));                                                   \
        exit(1);                                                               \
    }

#define FATAL(format, ...)                                                     \
    do {                                                                       \
        fprintf(stdout, "[ERROR] " format "\n", ##__VA_ARGS__);                \
        exit(1);                                                               \
    } while(0)

#define WARN(command, format, ...)                                             \
    if (command) {                                                             \
        fprintf(stdout, "[WARN] " format "\n", ##__VA_ARGS__);                 \
    }

#define WARN_RETURN(command, format, ...)                                      \
    if (command) {                                                             \
        fprintf(stdout, "[WARN] " format "\n", ##__VA_ARGS__);                 \
        return -1;                                                             \
    }

#define debug(format, ...)                                                     \
    if (DEBUG_ON) {                                                            \
        fprintf(stdout, "[DEBUG] " format "\n", ##__VA_ARGS__);                \
    }

#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

#define CEIL_DIV(a, b) (((a) + (b)-1) / (b))
#define NEXT(i, l) ((i + 1) % l)
#define PREV(i, l) ((i - 1) % l)
#define TRUNK_SIZE_BY_PAGE (150) // 150 pages
#define MAX_TRUNK_ID (80)
#define STORAGE_PREFIX "nflog_storage"

enum nflog_compression_t { COMPRESS_NONE, COMPRESS_LZ4, COMPRESS_ZSTD };
typedef struct __attribute__((packed)) _nflog_header_t {
    uint32_t                   id;                   /*     0     4 */
    uint32_t                   n_entries;            /*     4     4 */
    uint32_t                   max_n_entries;        /*     8     4 */
    uint32_t                   cksum;                /*    12     4 */
    enum nflog_compression_t   compression_opt;      /*    16     4 */
    time_t                     start_time;           /*    20     8 */
    time_t                     end_time;             /*    28     8 */

    /* size: 36, cachelines: 1, members: 7 */
    /* last cacheline: 36 bytes */
} nflog_header_t;

typedef struct __attribute__((packed)) _nflog_entry_t {
    // current timestamp since UNIX epoch
    time_t timestamp; /*     0     8 */

    // dest address
    struct in_addr daddr; /*     8     4 */

    // uid
    uint32_t uid; /*    12     4 */

    // unused space, just for padding
    uint8_t __unused1; /*    16     1 */

    // IP protocol (UDP or TCP)
    uint8_t protocol; /*    17     1 */

    // unused space, just for padding
    uint16_t __unused2; /*    18     2 */

    // source port
    uint16_t sport; /*    20     2 */

    // destination port
    uint16_t dport; /*    22     2 */

    /* size: 24, cachelines: 1, members: 8 */
} nflog_entry_t;

typedef struct _nflog_global_t {
    sem_t *nfl_commit_queue;
    uint16_t nfl_group_id;
    const char *storage_dir;
    enum nflog_compression_t compression_opt;
} nflog_global_t;

typedef struct _nflog_state_t {
    nflog_global_t *global;
    nflog_header_t *header;
    nflog_entry_t *store;

    struct nflog_handle *nfl_fd;
    struct nflog_g_handle *nfl_group_fd;

    bool has_finished;
    pthread_cond_t has_finished_cond;
    pthread_mutex_t has_finished_lock;
    pthread_t thread;
} nflog_state_t;

// only copy size of ipv4 header + tcp header
static const int nflog_recv_size = sizeof(struct iphdr) + sizeof(struct tcphdr);

#endif // _MAIN_H
