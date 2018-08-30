
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

// Number of packet to queue inside kernel before sending to userspsace.
// Setting this value to, e.g. 64 accumulates ten packets inside the
// kernel and transmits them as one netlink multipart message to userspace.
#define NF_NFLOG_QTHRESH 64

nfl_global_t g;

static void *nfl_start_commit_worker(void *targs);
static void nfl_commit(nfl_state_t *nf);
static void nfl_state_free(nfl_state_t *nf);

static int handle_packet(struct nflog_g_handle *gh, struct nfgenmsg *nfmsg,
                         struct nflog_data *nfa, void *_nf) {
#define HASH_ENTRY(e) (e->sport ^ e->timestamp)
    register const struct iphdr *iph;
    register nfl_entry_t *entry;
    const struct tcphdr *tcph;
    const struct udphdr *udph;
    char *payload;
    void *inner_hdr;
    uint32_t uid;

    // Store previous data hash (see HASH_ENTRY above) for rate-limiting purpose
    static uint64_t prev_entry_hash;

    int payload_len = nflog_get_payload(nfa, &payload);
    nfl_state_t *nf = (nfl_state_t *)_nf;

    // only process ipv4 packet
    if (unlikely(payload_len < 0) || ((payload[0] & 0xf0) != 0x40)) {
        debug("Ignore non-IPv4 packet");
        return 1;
    }

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
    } else {
        debug("Ignore non-TCP/UDP packet");
        return 1; // Ignore other types of packet
    }

    // get current timestamp
    time(&entry->timestamp);

    // Rate-limit incoming packets:
    // Ignore those with identical hash to prevent
    // packet flooding.  This simple trick is based
    // on the observation that packet surge usually
    // originates from one process.  Even if different
    // processes send simultaneously, the kernel deliver
    // packets in batch instead in interleaving manner.
    uint64_t entry_hash = HASH_ENTRY(entry);
    if (entry_hash == prev_entry_hash)
        return 1;
    prev_entry_hash = entry_hash;

    entry->daddr.s_addr = iph->daddr;
    entry->protocol = iph->protocol;

    // get sender uid
    if (nflog_get_uid(nfa, &uid) != 0)
        return 1;
    entry->uid = uid;

    // Advance to next entry
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

void nfl_open_netlink_fd(nfl_nl_t *nl, uint16_t group_id) {
    // open nflog
    ERR((nl->fd = nflog_open()) == NULL, "nflog_open")
    debug("Opening nflog communication file descriptor");

    // monitor IPv4 packets only
    ERR(nflog_bind_pf(nl->fd, AF_INET) < 0, "nflog_bind_pf");

    // bind to group
    nl->group_fd = nflog_bind_group(nl->fd, group_id);
    // If the returned group_fd is NULL, it's likely
    // that another process (like ulogd) has already
    // bound to the same NFLOD group.
    if(!nl->group_fd)
        FATAL("Cannot bind to NFLOG group %d, is it used by another process?", group_id);

    ERR(nflog_set_mode(nl->group_fd, NFULNL_COPY_PACKET, nfl_recv_size) < 0,
        "Could not set copy mode");

    // Batch send 128 packets from kernel to userspace
    ERR(nflog_set_qthresh(nl->group_fd, NF_NFLOG_QTHRESH),
        "Could not set qthresh");
}

void nfl_close_netlink_fd(nfl_nl_t *nl) {
    nflog_unbind_group(nl->group_fd);
    nflog_close(nl->fd);
}

void *nfl_collect_worker(void *targs) {
    nfl_state_t *nf = (nfl_state_t *)targs;
    memcpy(&g, nf->global, sizeof(nfl_global_t));

    nflog_callback_register(nf->netlink_fd->group_fd, &handle_packet, nf);
    debug("Registering nflog callback");

    int fd = nflog_fd(nf->netlink_fd->fd);
    debug("Recv worker #%u: main loop starts", nf->header->id);

    // Write start time
    time(&nf->header->start_time);

    int rv;
    // Must have at least 128 for each packet to account for
    // sizeof(struct iphdr) + sizeof(struct tcphdr) plus the
    // size of meta data needed by the library's data structure.
    char buf[128 * NF_NFLOG_QTHRESH + 1];
    while (nf->header->n_entries < nf->header->max_n_entries) {
        if ((rv = recv(fd, buf, sizeof(buf), 0)) && rv > 0) {
            debug("Recv worker #%u: nflog packet received "
                  "(len=%u, #entries=%u)",
                  nf->header->id, rv, nf->header->n_entries);
            nflog_handle_packet(nf->netlink_fd->fd, buf, rv);
        }
    }

    debug("Recv worker #%u: finished, received packets: %u",
          nf->header->id,
          nf->header->max_n_entries);

    // write end time
    time(&nf->header->end_time);

    // write checksum
    nf->header->cksum = nfl_header_cksum(nf->header);
    debug("Recv worker #%u: calculated checksum: %x",
          nf->header->id,
          nf->header->cksum);

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
    /* FIXME */
    bool truncate = true;

    sem_wait(g.nfl_commit_queue);
    debug("Comm worker #%u: commit started.", nf->header->id);
    nfl_commit_worker(nf->header, nf->store, g.compression_opt, truncate, filename);
    debug("Comm worker #%u: commit done.", nf->header->id);
    sem_post(g.nfl_commit_queue);

    nfl_state_free(nf);
    free((char *)filename);

    pthread_mutex_lock(&nf->has_finished_recv_lock);
    nf->has_finished_recv = true;
    pthread_cond_signal(&nf->has_finished_recv_cond);
    pthread_mutex_unlock(&nf->has_finished_recv_lock);

    pthread_exit(NULL);
}

/*
 * State managers
 */

void nfl_state_init(nfl_state_t **nf, uint32_t id, uint32_t entries_max,
                    nfl_global_t *g) {
    assert(nf);

    // Check if nf has been allocated
    if (unlikely(*nf == NULL)) {
        *nf = (nfl_state_t *)malloc(sizeof(nfl_state_t));
        (*nf)->global = g;
        (*nf)->header = (nfl_header_t *)malloc(sizeof(nfl_header_t));
        (*nf)->header->id = id;
        (*nf)->header->max_n_entries = entries_max;
        (*nf)->header->compression_opt = g->compression_opt;

        (*nf)->has_finished_recv = true;
        pthread_mutex_init(&(*nf)->has_finished_recv_lock, NULL);
        pthread_cond_init(&(*nf)->has_finished_recv_cond, NULL);
    }

    // Ensure trunk with same id in previous run has finished to prevent reusing
    // a trunk which it's still being used.  Furthermore, this hopefully
    // alleviate us from bursty network traffic.
    pthread_mutex_lock(&(*nf)->has_finished_recv_lock);
    while (!(*nf)->has_finished_recv)
        pthread_cond_wait(&(*nf)->has_finished_recv_cond, &(*nf)->has_finished_recv_lock);
    (*nf)->has_finished_recv = false;
    pthread_mutex_unlock(&(*nf)->has_finished_recv_lock);

    // Don't use calloc here, as it will cause page fault and
    // consume physical memory before we fill the buffer.
    // Instead, fill entries with 0 on the fly, to squeeze
    // more space for compression.
    (*nf)->store = (nfl_entry_t *)malloc(sizeof(nfl_entry_t) * entries_max);
    (*nf)->header->n_entries = 0;
}

static void nfl_state_free(nfl_state_t *nf) {
    // Free only packet store and leave the rest intact
    free((void *)nf->store);
}
