
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

#include "commit.h"
#include "main.h"
#include <libnetfilter_log/libnetfilter_log.h>
#include <pthread.h>
#include <stddef.h> // size_t for libnetfilter_log
#include <stdint.h>
#include <string.h>
#include <sys/types.h> // u_int32_t for libnetfilter_log
#include <time.h>

// Number of packet to queue inside kernel before sending to userspsace.
// Setting this value to, e.g. 64 accumulates ten packets inside the
// kernel and transmits them as one netlink multipart message to userspace.
#define NF_NFLOG_QTHRESH 64

Global g;

static int handle_packet(__attribute__((unused)) struct nflog_g_handle *gh,
                         __attribute__((unused)) struct nfgenmsg *nfmsg,
                         struct nflog_data *nfa, void *_s) {
#define HASH_ENTRY(e) (e->sport ^ e->timestamp)
    register const struct iphdr *iph;
    register Entry *entry;
    const struct tcphdr *tcph;
    const struct udphdr *udph;
    char *payload;
    void *inner_hdr;
    uint32_t uid;

    // Store previous data hash (see HASH_ENTRY above) for rate-limiting purpose
    static uint64_t prev_entry_hash;

    int payload_len = nflog_get_payload(nfa, &payload);
    State *s = (State *)_s;

    // only process ipv4 packet
    if (unlikely(payload_len < 0) || ((payload[0] & 0xf0) != 0x40)) {
        DEBUG("Ignore non-IPv4 packet");
        return 1;
    }

    if (unlikely(s->header->nr_entries >= g.max_nr_entries))
        return 1;

    iph = (struct iphdr *)payload;
    entry = &(s->store[s->header->nr_entries]);

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
        DEBUG("Ignore non-TCP/UDP packet");
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
    s->header->nr_entries++;

    DEBUG("Recv packet info entry #%d: "
          "timestamp:\t%ld,\t"
          "daddr:\t%ld,\t"
          "transfer:\t%s,\t"
          "uid:\t%d,\t"
          "sport:\t%d,\t"
          "dport:\t%d",
          s->header->nr_entries, entry->timestamp,
          (unsigned long)entry->daddr.s_addr,
          iph->protocol == IPPROTO_TCP ? "TCP" : "UDP", entry->uid,
          entry->sport, entry->dport);

    // Ignore IPv6 packet for now Q_Q
    return 0;
}

void collect_open_netlink(Netlink *nl, uint16_t group_id) {
    // open nflog
    if ((nl->fd = nflog_open()) == NULL) {
        FATAL("nflog_open failed");
    }

    DEBUG("Opening nflog communication file descriptor");

    // monitor IPv4 packets only
    if (nflog_bind_pf(nl->fd, AF_INET) < 0) {
        FATAL("nflog_bind_pf failed");
    }

    // bind to group
    nl->group_fd = nflog_bind_group(nl->fd, group_id);
    // If the returned group_fd is NULL, it's likely
    // that another process (like ulogd) has already
    // bound to the same NFLOD group.
    if (!nl->group_fd)
        FATAL("Cannot bind to NFLOG group %d, is it used by another process?",
              group_id);

    // only copy size of ipv4 header + tcp header
    int recv_size = sizeof(struct iphdr) + sizeof(struct tcphdr);
    if (nflog_set_mode(nl->group_fd, NFULNL_COPY_PACKET, recv_size) < 0)
        FATAL("Could not set copy mode");

    // Batch send 128 packets from kernel to userspace
    if (nflog_set_qthresh(nl->group_fd, NF_NFLOG_QTHRESH))
        FATAL("Could not set qthresh");
}

void collect_close_netlink(Netlink *nl) {
    nflog_unbind_group(nl->group_fd);
    nflog_close(nl->fd);
}

void *collect_worker(void *targs) {
    State *s = (State *)targs;
    memcpy(&g, s->global, sizeof(Global));

    nflog_callback_register(s->netlink_fd->group_fd, &handle_packet, s);
    DEBUG("Registering nflog callback");

    int fd = nflog_fd(s->netlink_fd->fd);
    DEBUG("Recv worker #%lu: main loop starts", pthread_self());

    // Write start time
    time(&s->header->start_time);

    int rv;
    // Must have at least 128 for each packet to account for
    // sizeof(struct iphdr) + sizeof(struct tcphdr) plus the
    // size of meta data needed by the library's data structure.
    char buf[128 * NF_NFLOG_QTHRESH + 1];
    while (s->header->nr_entries < g.max_nr_entries) {
        if ((rv = recv(fd, buf, sizeof(buf), 0)) && rv > 0) {
            DEBUG("Recv worker #%lu: packet received "
                  "(len=%u, #entries=%u)",
                  pthread_self(), rv, s->header->nr_entries);
            nflog_handle_packet(s->netlink_fd->fd, buf, rv);
        }
    }

    // write end time
    time(&s->header->end_time);
    s->header->raw_size = s->header->nr_entries * sizeof(Entry);

    pthread_t tid;
    pthread_create(&tid, NULL, commit, (void *)s);
    pthread_detach(tid);
    return NULL;
}

void state_init(State **s, Netlink *nl, Global *g) {
    assert(s);
    *s = (State *)malloc(sizeof(State));
    (*s)->global = g;
    (*s)->netlink_fd = nl;
    (*s)->header = (Header *)calloc(sizeof(Header), 1);
    (*s)->header->compression_type = g->compression_type;

    (*s)->store = (Entry *)malloc(sizeof(Entry) * g->max_nr_entries);
    (*s)->header->nr_entries = 0;
}

void state_free(State *s) {
    free(s->store);
    free(s->header);
    free(s);
}
