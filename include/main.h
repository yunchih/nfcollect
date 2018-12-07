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

#ifndef _MAIN_H
#define _MAIN_H
#include <arpa/inet.h>
#include <assert.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>

// Global variables
#define g_sqlite_table_header "nfcollect_v1_header"
#define g_sqlite_table_data "nfcollect_v1_data"
#define g_sqlite_nr_fail_retry 8
#define g_gc_rate 16
// Default number of packets stored in a block
#define g_max_nr_entries_default (256*1024/24)
#ifdef DEBUG_OUTPUT
#define DEBUG_ON 1
#else
#define DEBUG_ON 0
#endif

#define ASSERT(condition, error_msg)                                           \
    if (!(condition)) {                                                        \
        fputs((error_msg), stderr);                                            \
        exit(1);                                                               \
    }

#define ERROR(format, ...)                                                     \
    fprintf(stdout, "[ERROR] " format "\n", ##__VA_ARGS__);

#define FATAL(format, ...)                                                     \
    do {                                                                       \
        fprintf(stdout, "[FATAL] " format "\n", ##__VA_ARGS__);                \
        exit(1);                                                               \
    } while (0)

#define WARN(format, ...) fprintf(stdout, "[WARN] " format "\n", ##__VA_ARGS__);

#define WARN_RETURN(command, format, ...)                                      \
    if (command) {                                                             \
        fprintf(stdout, "[WARN] " format "\n", ##__VA_ARGS__);                 \
        return -1;                                                             \
    }

#define DEBUG(format, ...)                                                     \
    if (DEBUG_ON) {                                                            \
        fprintf(stdout, "[DEBUG] " format "\n", ##__VA_ARGS__);                \
    }

#define INFO(format, ...) fprintf(stdout, "[INFO] " format "\n", ##__VA_ARGS__);

#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

#ifdef __GNUC__
#define UNUSED_FUNCTION(x) __attribute__((__unused__)) UNUSED_ ## x
#else
#define UNUSED_FUNCTION(x) UNUSED_ ## x
#endif

enum CompressionType { COMPRESS_NONE, COMPRESS_LZ4, COMPRESS_ZSTD };

typedef struct _Header {
    uint32_t nr_entries;
    uint32_t raw_size;
    enum CompressionType compression_type;
    time_t start_time;
    time_t end_time;
} Header;

typedef struct __attribute__((packed)) _Entry {
    // current timestamp since UNIX epoch
    time_t timestamp;
    // dest address
    struct in_addr daddr;
    // uid
    uint32_t uid;
    // unused space, just for padding
    uint8_t __unused1;
    // IP protocol (UDP or TCP)
    uint8_t protocol;
    // unused space, just for padding
    uint16_t __unused2;
    // source port
    uint16_t sport;
    // destination port
    uint16_t dport;

    /* size: 24, cachelines: 1, members: 8 */
} Entry;

typedef struct _nfl_nl_t {
    struct nflog_handle *fd;
    struct nflog_g_handle *group_fd;
} Netlink;

typedef struct _Global {
    uint16_t nl_group_id;

    uint32_t storage_budget;
    uint32_t storage_consumed;
    pthread_mutex_t storage_consumed_lock;

    uint32_t max_nr_entries;
    const char *storage_file;
    enum CompressionType compression_type;
} Global;

typedef struct _State {
    Header *header;
    Entry *store;
    Netlink *netlink_fd;
    Global *global;
} State;

typedef struct _Timerange {
    time_t from, until;
} Timerange;

typedef void (*StateCallback)(const State *s, const Timerange *t);

#endif // _MAIN_H
