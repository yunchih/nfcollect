
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
#include <libnetfilter_log/libnetfilter_log.h>
#include <pthread.h>
#include <stddef.h> // size_t for libnetfilter_log
#include <string.h>
#include <sys/types.h> // u_int32_t for libnetfilter_log
#include <time.h>

nfl_global_t g;

static void nfl_init(nfl_state_t *nf);
static void *nfl_start_commit_worker(void *targs);
static void nfl_commit(nfl_state_t *nf);
static void nfl_state_free(nfl_state_t *nf);

static int handle_packet(struct nflog_g_handle *gh, struct nfgenmsg *nfmsg,
                         struct nflog_data *nfa, void *_nf) {
    register const struct iphdr *iph;
    register nfl_entry_t *entry;
    const struct tcphdr *tcph;
    const struct udphdr *udph;
    char *payload;
    void *inner_hdr;
    uint32_t uid;

    int payload_len = nflog_get_payload(nfa, &payload);
    nfl_state_t *nf = (nfl_state_t *)_nf;

    // only process ipv4 packet
    if (unlikely(payload_len < 0) || ((payload[0] & 0xf0) != 0x40))
        return 1;
    if (unlikely(nf->header->n_entries >= nf->header->max_n_entries))
        return 1;

    iph = (struct iphdr *)payload;
    entry = &(nf->store[nf->header->n_entries]);

    inner_hdr = (uint32_t *)iph + iph->ihl;
    // Only accept TCP / UDP packets
    if (iph->protocol == IPPROTO_TCP) {
        tcph = (struct tcphdr *)inner_hdr;
        entry->sport = ntohs(tcph->source);
        entry->dport = ntohs(tcph->dest);

        // only process SYNC and PSH packet, drop ACK
        if (!tcph->syn && !tcph->psh)
            return 1;
    } else if (iph->protocol == IPPROTO_UDP) {
        udph = (struct udphdr *)inner_hdr;
        entry->sport = ntohs(udph->source);
        entry->dport = ntohs(udph->dest);
    } else
        return 1; // Ignore other types of packet

    entry->daddr.s_addr = iph->daddr;
    entry->protocol = iph->protocol;

    // get sender uid
    if (nflog_get_uid(nfa, &uid) != 0)
        return 1;
    entry->uid = uid;

    // get current timestamp
    time(&entry->timestamp);
    nf->header->n_entries++;

    debug("Recv packet info entry #%d: "
          "timestamp:\t%ld,\t"
          "daddr:\t%ld,\t"
          "transfer:\t%s,\t"
          "uid:\t%d,\t"
          "sport:\t%d,\t"
          "dport:\t%d",
          nf->header->n_entries, entry->timestamp,
          (unsigned long)entry->daddr.s_addr,
          iph->protocol == IPPROTO_TCP ? "TCP" : "UDP", entry->uid,
          entry->sport, entry->dport);

    // Ignore IPv6 packet for now Q_Q
    return 0;
}

static void nfl_init(nfl_state_t *nf) {
    // open nflog
    ERR((nf->nfl_fd = nflog_open()) == NULL, "nflog_open")
    debug("Opening nflog communication file descriptor");

    // monitor IPv4 packets only
    ERR(nflog_bind_pf(nf->nfl_fd, AF_INET) < 0, "nflog_bind_pf");

    // bind to group
    nf->nfl_group_fd = nflog_bind_group(nf->nfl_fd, nf->global->nfl_group_id);

    /* ERR(nflog_set_mode(nf->nfl_group_fd, NFULNL_COPY_PACKET, sizeof(struct
     * iphdr) + 4) < 0, */
    ERR(nflog_set_mode(nf->nfl_group_fd, NFULNL_COPY_PACKET, nfl_recv_size) < 0,
        "Could not set copy mode");

    nflog_callback_register(nf->nfl_group_fd, &handle_packet, nf);
    debug("Registering nflog callback");

    memcpy(&g, nf->global, sizeof(nfl_global_t));
}

void *nfl_collect_worker(void *targs) {
    nfl_state_t *nf = (nfl_state_t *)targs;
    nfl_init(nf);

    int fd = nflog_fd(nf->nfl_fd);
    uint32_t *p_cnt_now = &(nf->header->n_entries);
    uint32_t cnt_max = nf->header->max_n_entries;

    debug("Recv worker #%u: main loop starts", nf->header->id);
    time(&nf->header->start_time);

    int rv;
    // Must have at least 128 to account for sizeof(struct iphdr) +
    // sizeof(struct tcphdr)
    // plus the size of meta data needed by the library's data structure
    char buf[128];
    while (*p_cnt_now < cnt_max) {
        if ((rv = recv(fd, buf, sizeof(buf), 0)) && rv > 0) {
            debug("Recv worker #%u: nflog packet received (len=%u)",
                  nf->header->id, rv);
            nflog_handle_packet(nf->nfl_fd, buf, rv);
        }
    }

    debug("Recv worker #%u: finish recv, received packets: %u", nf->header->id,
          cnt_max);

    // write end time
    time(&nf->header->end_time);
    nflog_unbind_group(nf->nfl_group_fd);
    nflog_close(nf->nfl_fd);

    // write checksum
    nf->header->cksum = nfl_header_cksum(nf->header);

    // spawn commit thread
    nfl_commit(nf);
    pthread_exit(NULL);
}

/*
 * Committer
 */

static void nfl_commit(nfl_state_t *nf) {
    pthread_t tid;
    pthread_create(&tid, NULL, nfl_start_commit_worker, (void *)nf);
    pthread_detach(tid);
}

static void *nfl_start_commit_worker(void *targs) {
    nfl_state_t *nf = (nfl_state_t *)targs;
    const char *filename = nfl_get_filename(g.storage_dir, nf->header->id);
    debug("Comm worker #%u: thread started.", nf->header->id);

    sem_wait(g.nfl_commit_queue);
    debug("Comm worker #%u: commit started.", nf->header->id);
    nfl_commit_worker(nf->header, nf->store, g.compression_opt, filename);
    debug("Comm worker #%u: commit done.", nf->header->id);
    sem_post(g.nfl_commit_queue);

    nfl_state_free(nf);
    free((char *)filename);

    pthread_mutex_lock(&nf->has_finished_lock);
    nf->has_finished = true;
    pthread_cond_signal(&nf->has_finished_cond);
    pthread_mutex_unlock(&nf->has_finished_lock);

    pthread_exit(NULL);
}

/*
 * State managers
 */

void nfl_state_init(nfl_state_t **nf, uint32_t id, uint32_t entries_max,
                    nfl_global_t *g) {
    assert(nf);
    if (unlikely(*nf == NULL)) {
        *nf = (nfl_state_t *)malloc(sizeof(nfl_state_t));
        (*nf)->global = g;
        (*nf)->header = (nfl_header_t *)malloc(sizeof(nfl_header_t));
        (*nf)->header->id = id;
        (*nf)->header->n_entries = 0;
        (*nf)->header->max_n_entries = entries_max;
        (*nf)->header->compression_opt = g->compression_opt;

        (*nf)->has_finished = true;
        pthread_mutex_init(&(*nf)->has_finished_lock, NULL);
        pthread_cond_init(&(*nf)->has_finished_cond, NULL);
    }

    // Ensure trunk with same id in previous run has finished to prevent reusing
    // a trunk which it's still being used.  Furthermore, this hopefully
    // alleviate us
    // from bursty network traffic.
    pthread_mutex_lock(&(*nf)->has_finished_lock);
    while (!(*nf)->has_finished)
        pthread_cond_wait(&(*nf)->has_finished_cond, &(*nf)->has_finished_lock);
    (*nf)->has_finished = false;
    pthread_mutex_unlock(&(*nf)->has_finished_lock);

    // Don't use calloc here, as it will cause page fault and
    // consume physical memory before we fill the buffer.
    // Instead, fill entries with 0 on the fly, to squeeze
    // more space for compression.
    (*nf)->store = (nfl_entry_t *)malloc(sizeof(nfl_entry_t) * entries_max);
}

static void nfl_state_free(nfl_state_t *nf) {
    // Free only packet store and leave the rest intact
    free((void *)nf->store);
}
