/*
 * Copyright (C) 2023 Federico Parola <federico.parola@polito.it>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <h2os/net.hh>
#include <h2os/sock_queue.hh>
#include <osv/debug.h>
#include "../drivers/virtio-shm-xchg.hh"

#define EPHEMERAL_PORTS_FIRST 1024
#define EPHEMERAL_PORTS_COUNT (0xffff - EPHEMERAL_PORTS_FIRST + 1)

struct h2os_socket {
    // Following two fields are 0 if socket is not connected
    uint32_t raddr;
    uint16_t rport;
    uint16_t lport;  // 0 if socket not bound
    struct sock_queue *rx_queue;
};

// At most one socket per local port for now. Is this struct huge? 512 KB most
// of which are unused
struct h2os_socket *h2os_sockets[USHRT_MAX + 1];
uint16_t last_assigned_port = EPHEMERAL_PORTS_FIRST - 1;

struct h2os_socket *h2os_socket_open()
{
    struct h2os_socket *s = new h2os_socket();
    if (!s) {
        return nullptr;
    }

    s->rx_queue = sock_queue_create();
    if (!s->rx_queue) {
        delete s;
        return nullptr;
    }

    return s;
}

void h2os_socket_close(struct h2os_socket *s)
{
    if (s->lport) {
        h2os_sockets[s->lport] = NULL;
    }

    sock_queue_free(s->rx_queue);
    delete s;
}

int h2os_socket_bind(struct h2os_socket *s, uint16_t port)
{
    if (!s || port == 0 || s->lport || h2os_sockets[port]) {
        return -1;
    }

    // TODO: handle concurrent access
    h2os_sockets[port] = s;

    return 0;
}

// Super dummy algorithm
// TODO: handle concurrent access
static int assign_local_port(struct h2os_socket *s) {    
    uint16_t p = last_assigned_port + 1;
    int c;
    for (c = EPHEMERAL_PORTS_COUNT; c > 0; c--) {
        if (!h2os_sockets[p]) {
            s->lport = p;
            last_assigned_port = p;
            h2os_sockets[p] = s;
            break;
        }

        if (++p == 0) {
            p = EPHEMERAL_PORTS_FIRST;
        }
    }

    if (c == 0) {
        return -1;
    }

    return 0;
}

int h2os_xmit_desc(struct h2os_socket *s, const struct h2os_shm_desc *desc,
        const struct h2os_endpoint *dst)
{
    if (!s || !desc || !dst) {
        return -1;
    }

    if (!s->lport) {
        if (assign_local_port(s)) {
            kprintf("h2os: could not find an available local port\n");
            return -1;
        }
    }

    struct h2os_pkt pkt = {
        .shm_desc = *desc,
        .hdr = {
            .saddr = 0,
            .daddr = dst->addr,
            .sport = s->lport,
            .dport = dst->port,
            .proto = 0
        }
    };

    return virtio::shm_xchg::get_instance()->xmit_pkt(&pkt);
}

int h2os_recv_desc(struct h2os_socket *s, struct h2os_shm_desc *desc,
        struct h2os_endpoint *src)
{
    if (!s || !desc) {
        return -1;
    }

    sock_queue_consume(s->rx_queue, desc);

    if (src) {
        // Need to pass the src in the rx ring to populate this field
        src = 0;
    }

    return 0;
}

// Called from the driver
int h2os_handle_pkt(struct h2os_pkt *pkt)
{
    if (!pkt) {
        return -1;
    }

    struct h2os_socket *s = h2os_sockets[pkt->hdr.dport];
    if (!s) {
        kprintf("h2os: could not find matching socket\n");
        return -1;
    }

    return sock_queue_produce(s->rx_queue, &pkt->shm_desc);
}

int h2os_get_dev_stats(struct h2os_dev_stats *stats)
{
    return virtio::shm_xchg::get_instance()->get_stats(stats);
}

int h2os_get_queue_stats(unsigned queue, struct h2os_dev_stats *stats)
{
    return virtio::shm_xchg::get_instance()->get_queue_stats(queue, stats);
}