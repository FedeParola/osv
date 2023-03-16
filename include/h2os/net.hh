/*
 * Copyright (C) 2023 Federico Parola <federico.parola@polito.it>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef H2OS_NET_HH
#define H2OS_NET_HH

#include <stdint.h>

namespace h2os {
namespace net {

enum socket_type {
    CONNECTED,
    CONNLESS
};

struct endpoint {
    uint32_t addr;
    uint16_t port;
};

// TODO: move fields not needed for forwarding to shm
struct hdr {
    uint32_t saddr;
    uint32_t daddr;
    uint16_t sport;
    uint16_t dport;
    socket_type type;
};

// Fields can be shrinked by limiting the size of the shmem and the minimum size
// of a shm buffer
struct shm_desc {
    uint64_t addr;
    uint64_t len;
};

struct pkt {
    struct shm_desc shm_desc;
    struct hdr hdr;  // Actually a trailer to guarantee aligment
};

struct dev_stats {
    unsigned long rx_pkts;
    unsigned long rx_sockq_full;
    unsigned long rx_wakeups;
    unsigned long tx_pkts;
    unsigned long tx_errors;
};

bool handle_pkt(const pkt& pkt);
void get_dev_stats(dev_stats& stats);
void get_queue_stats(dev_stats& stats);

}  // net
}  // h2os

#endif  // H2OS_NET_HH