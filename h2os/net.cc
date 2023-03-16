/*
 * Copyright (C) 2023 Federico Parola <federico.parola@polito.it>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <h2os/net.hh>
#include <h2os/sock_queue.hh>
#include <osv/debug.h>
#include <osv/mutex.h>
#include <osv/rcu-hashtable.hh>
#include "../drivers/virtio-shm-xchg.hh"

#define EPHEMERAL_PORTS_FIRST 1024
#define EPHEMERAL_PORTS_COUNT (0xffff - EPHEMERAL_PORTS_FIRST + 1)

struct h2os_socket_id {
    // Following two fields are 0 if socket is not connected
    uint32_t raddr;
    uint16_t rport;
    uint16_t lport;  // 0 if socket not bound
    enum h2os_sock_type type;
};

struct sockid_hash {
    std::size_t operator()(const h2os_socket_id& sid) const noexcept {
        std::size_t h1 = std::hash<uint32_t>{}(sid.raddr);
        std::size_t h2 = std::hash<uint16_t>{}(sid.rport);
        std::size_t h3 = std::hash<uint16_t>{}(sid.lport);
        std::size_t h4 = std::hash<enum h2os_sock_type>{}(sid.type);
        // Combine hash as suggested by the cppreference:
        // https://en.cppreference.com/w/cpp/utility/hash#Example
        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
    }
};

struct h2os_socket {
    h2os_socket_id id;
    sock_queue *rx_queue;
};

struct sockptr_hash {
    std::size_t operator()(const h2os_socket *s) const noexcept {
        return sockid_hash{}(s->id);
    }
};

struct sockid_equal {
    bool operator()(const h2os_socket_id& sid, const h2os_socket *s)
            const noexcept {
        return !memcmp(&sid, &s->id, sizeof(sid));
    }
};

static uint32_t local_addr;
static osv::rcu_hashtable<h2os_socket *, sockptr_hash> sockets;
static mutex sockets_mtx;
static uint16_t last_assigned_port = EPHEMERAL_PORTS_FIRST - 1;

h2os_socket *h2os_socket_open(enum h2os_sock_type type)
{
    h2os_socket *s = new h2os_socket();
    if (!s) {
        return nullptr;
    }

    s->id.type = type;

    s->rx_queue = sock_queue_create();
    if (!s->rx_queue) {
        delete s;
        return nullptr;
    }

    return s;
}

void h2os_socket_close(struct h2os_socket *s)
{
    // Remove the socket form the sockets map if present. A socket is stored if
    // it is bound to a local port
    if (s->id.lport) {
        sockets_mtx.lock();
        auto sockit = sockets.owner_find(s);
        if (sockit) {
            sockets.erase(sockit);
        }
        sockets_mtx.unlock();
    }

    sock_queue_free(s->rx_queue);
    delete s;
}

int h2os_socket_bind(struct h2os_socket *s, uint16_t port)
{
    int ret = 0;

    if (!s || port == 0 || s->id.lport) {
        return -1;
    }

    s->id.lport = port;

    sockets_mtx.lock();
    // Check that the port isn't already used
    if (sockets.owner_find(s)) {
        s->id.lport = 0;
        ret = -1;
    } else {
        sockets.insert(s);
    }
    sockets_mtx.unlock();

    return ret;
}

// Super dummy algorithm
// Could block all other bind/close for a long time if it cannot find a port
static int assign_local_port(struct h2os_socket *s)
{
    int c;
    sockets_mtx.lock();
    for (c = EPHEMERAL_PORTS_COUNT; c > 0; c--) {
        s->id.lport = ++last_assigned_port;
        if (!sockets.owner_find(s)) {
            sockets.insert(s);
            break;
        }

        // Wrap
        if (last_assigned_port == 0) {
            last_assigned_port= EPHEMERAL_PORTS_FIRST - 1;
        }
    }
    sockets_mtx.unlock();

    if (c == 0) {
        s->id.lport = 0;
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

    if (!s->id.lport) {
        if (assign_local_port(s)) {
            kprintf("h2os: could not find an available local port\n");
            return -1;
        }
    }

    h2os_pkt pkt = {
        .shm_desc = *desc,
        .hdr = {
            .saddr = local_addr,
            .daddr = dst->addr,
            .sport = s->id.lport,
            .dport = dst->port,
            .type = s->id.type
        }
    };

    return virtio::shm_xchg::get_instance()->xmit_pkt(&pkt);
}

int h2os_recv_desc(struct h2os_socket *s, struct h2os_shm_desc *desc,
        struct h2os_endpoint *src)
{
    if (!s || !desc || !s->id.lport) {
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

    h2os_socket_id sid = {
        .raddr = pkt->hdr.saddr,
        .rport = pkt->hdr.sport,
        .lport = pkt->hdr.dport,
        .type = pkt->hdr.type
    };
    WITH_LOCK(osv::rcu_read_lock) {
        auto sockit = sockets.reader_find(sid, sockid_hash(), sockid_equal());
        if (!sockit) {
            kprintf("h2os: could not find matching socket\n");
            return -1;
        }
        h2os_socket *s = *sockit;

        return sock_queue_produce(s->rx_queue, &pkt->shm_desc);
    }
}

int h2os_get_dev_stats(struct h2os_dev_stats *stats)
{
    return virtio::shm_xchg::get_instance()->get_stats(stats);
}

int h2os_get_queue_stats(unsigned queue, struct h2os_dev_stats *stats)
{
    return virtio::shm_xchg::get_instance()->get_queue_stats(queue, stats);
}