/*
 * Copyright (C) 2023 Federico Parola <federico.parola@polito.it>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef H2OS_NET_HH
#define H2OS_NET_HH

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum h2os_sock_type {
    H2OS_SOCK_TYPE_CONNECTED,
    H2OS_SOCK_TYPE_CONNLESS
};

struct h2os_endpoint {
    uint32_t addr;
    uint16_t port;
};

// TODO: move fields not needed for forwarding to shm
struct h2os_hdr {
    uint32_t saddr;
    uint32_t daddr;
    uint16_t sport;
    uint16_t dport;
    enum h2os_sock_type type;
};

// Fields can be shrinked by limiting the size of the shmem and the minimum size
// of a shm buffer
struct h2os_shm_desc {
    uint64_t addr;
    uint64_t len;
};

struct h2os_pkt {
    struct h2os_shm_desc shm_desc;
    struct h2os_hdr hdr;  // Actually a trailer to guarantee aligment
};

// Opaque socket type
struct h2os_socket;

struct h2os_dev_stats {
    unsigned long rx_pkts;
    unsigned long rx_sockq_full;
    unsigned long rx_wakeups;
    unsigned long tx_pkts;
    unsigned long tx_errors;
};

struct h2os_socket *h2os_socket_open(enum h2os_sock_type type);
void h2os_socket_close(struct h2os_socket *s);
int h2os_socket_bind(struct h2os_socket *s, uint16_t port);
int h2os_socket_listen(struct h2os_socket *s);
struct h2os_socket *h2os_socket_accept(struct h2os_socket *s);
int h2os_socket_connect(struct h2os_socket *s, struct h2os_endpoint dst);
int h2os_xmit_desc(struct h2os_socket *s, const struct h2os_shm_desc *desc,
        const struct h2os_endpoint *dst);
int h2os_recv_desc(struct h2os_socket *s, struct h2os_shm_desc *desc,
        struct h2os_endpoint *src);
int h2os_handle_pkt(struct h2os_pkt *pkt);
int h2os_get_dev_stats(struct h2os_dev_stats *stats);
int h2os_get_queue_stats(unsigned queue, struct h2os_dev_stats *stats);

#ifdef __cplusplus
}
#endif

#endif  // H2OS_NET_HH