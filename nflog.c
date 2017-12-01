
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
#include "nflog.h"
#include "main.h"
#include <stddef.h>    // size_t for libnetfilter_log
#include <sys/types.h> // u_int32_t for libnetfilter_log
#include <libnetfilter_log/libnetfilter_log.h>
#include <pthread.h>
#include <time.h>

extern sem_t nfl_commit_queue;
extern uint16_t nfl_group_id;

static void nfl_cleanup(nflog_state_t *nf);
static void nfl_init(nflog_state_t *nf);
static void *_nfl_commit_worker(void *targs);
static void nfl_commit(nflog_state_t *nf);

static int handle_packet(struct nflog_g_handle *gh, struct nfgenmsg *nfmsg,
                         struct nflog_data *nfa, void *_nf) {
    char *payload;
    int payload_len = nflog_get_payload(nfa, &payload);
    nflog_state_t *nf = (nflog_state_t *)_nf;

    // only process ipv4 packet
    if (payload_len >= 0 && ((payload[0] & 0xf0) == 0x40)) {
        struct iphdr *iph = (struct iphdr *)payload;
        nflog_entry_t *entry = &(nf->store[nf->header->n_entries]);

        void *inner_hdr = iph + iph->ihl;
        // Only accept TCP / UDP packets
        if (iph->protocol == IPPROTO_TCP) {
            struct tcphdr *tcph = (struct tcphdr *)inner_hdr;
            entry->sport = ntohs(tcph->source);
            entry->dport = ntohs(tcph->dest);
        } else if (iph->protocol == IPPROTO_UDP) {
            struct udphdr *tcph = (struct udphdr *)inner_hdr;
            entry->sport = ntohs(tcph->source);
            entry->dport = ntohs(tcph->dest);
        } else
            return 1; // Ignore other types of packet

        entry->daddr.s_addr = iph->daddr;
        entry->protocol = iph->protocol;

        // get sender uid
        uint32_t uid;
        if (nflog_get_uid(nfa, &uid) == 0)
            entry->uid = uid;
        else
            entry->uid = (uint32_t)~0;

        // get current timestamp
        entry->timestamp = time(NULL);
        nf->header->n_entries++;
    }

    // Ignore IPv6 packet for now Q_Q
    return 0;
}

static void nfl_init(nflog_state_t *nf) {
    // open nflog
    ERR((nf->nfl_fd = nflog_open()) == NULL, "error during nflog_open()")

    // monitor IPv4 packets only
    ERR(nflog_bind_pf(nf->nfl_fd, AF_INET) < 0, "error during nflog_bind_pf()");

    // bind to group
    nf->nfl_group_fd = nflog_bind_group(nf->nfl_fd, nfl_group_id);

    // only copy size of ipv4 header + tcp/udp src/dest port (first 4 bytes of their headers
    ERR(nflog_set_mode(nf->nfl_group_fd, NFULNL_COPY_PACKET, sizeof(struct iphdr) + 4) < 0,
        "Could not set copy mode");

    nflog_callback_register(nf->nfl_group_fd, &handle_packet, NULL);
}

static void nfl_cleanup(nflog_state_t *nf) {
    nflog_unbind_group(nf->nfl_group_fd);
    nflog_close(nf->nfl_fd);
}

void *nflog_worker(void *targs) {
    nflog_state_t *nf = (nflog_state_t *)targs;
    nfl_init(nf);

    int fd = nflog_fd(nf->nfl_fd);
    uint32_t *p_cnt_now = &(nf->header->n_entries);
    uint32_t cnt_max = nf->header->max_n_entries;

    debug("Recv worker #%u: main loop starts\n", nf->header->id);
    nf->header->start_time = time(NULL);
    while (*p_cnt_now < cnt_max) {
        int rv; char buf[4096];
        if ((rv = recv(fd, buf, sizeof(buf), 0)) && rv >= 0) {
            debug("Recv worker #%u: nflog packet received (len=%u)\n", nf->header->id,
                  rv);
            nflog_handle_packet(nf->nfl_fd, buf, rv);
        }
    }

    nf->header->end_time = time(NULL);
    nfl_cleanup(nf);
    nfl_commit(nf);

    /* TODO: can return exit status */
    pthread_exit(NULL);
}

/*
 * Committer
 */

void nfl_commit(nflog_state_t *nf) {
    pthread_t tid;
    pthread_create(&tid, NULL, _nfl_commit_worker, (void *)nf);
    pthread_detach(tid);
}

void *_nfl_commit_worker(void *targs) {
    nflog_state_t* nf = (nflog_state_t*) targs;
    debug("Comm worker #%u: thread started\n", nf->header->id);

    sem_wait(&nfl_commit_queue);
    debug("Comm worker #%u: commit started\n", nf->header->id);
    nfl_commit_worker(nf->header, nf->store);
    debug("Comm worker #%u: commit done\n", nf->header->id);
    sem_post(&nfl_commit_queue);

    // Commit finished
    nfl_state_free(nf);
    pthread_mutex_unlock(&(nf->lock));
}

/*
 * State managers
 */

void nfl_state_update_or_create(nflog_state_t **nf, uint32_t id, uint32_t entries_max) {
    if(*nf == NULL) {
        *nf = (nflog_state_t *)malloc(sizeof(nflog_state_t));
        pthread_mutex_init(&((*nf)->lock), NULL);
    }

    // Don't use calloc here, as it will consume physical memory
    // before we fill the buffer.  Instead, fill entries with 0
    // on the fly, to squeeze more space for compression.
    (*nf)->store = (nflog_entry_t *)malloc(sizeof(nflog_entry_t) *
                                                  entries_max);
    (*nf)->header->id = id;
    (*nf)->header->max_n_entries = entries_max;
    (*nf)->header->n_entries = 0;
}

void nfl_state_free(nflog_state_t *nf) {
    // Free header and store only
    // Leave the rest intact
    free(nf->header);
    free(nf->store);
}
