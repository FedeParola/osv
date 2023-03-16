/*
 * Copyright (C) 2023 Federico Parola <federico.parola@polito.it>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <h2os/socket.hh>
#include <osv/debug.h>
#include <osv/mutex.h>
#include <osv/rcu-hashtable.hh>
#include "../drivers/virtio-shm-xchg.hh"

#define EPHEMERAL_PORTS_FIRST 1024
#define EPHEMERAL_PORTS_COUNT (0xffff - EPHEMERAL_PORTS_FIRST + 1)

namespace h2os {
namespace net {

struct sockptr_hash {
    std::size_t operator()(const socket *s) const noexcept {
        return socket::id::hash{}(s->get_id());
    }
};

static uint32_t local_addr;
// Consider using weak_ptr to sockets or something else
static osv::rcu_hashtable<socket *, sockptr_hash> sockets;
static mutex sockets_mtx;
static uint16_t last_assigned_port = EPHEMERAL_PORTS_FIRST - 1;

// Called from the driver
bool handle_pkt(const pkt& pkt)
{
    socket::id sid = {
        .raddr = pkt.hdr.saddr,
        .rport = pkt.hdr.sport,
        .lport = pkt.hdr.dport,
        .type = pkt.hdr.type
    };
    WITH_LOCK(osv::rcu_read_lock) {
        auto sockit = sockets.reader_find(sid, socket::id::hash(),
                socket::id::equal());
        if (!sockit) {
            kprintf("h2os: could not find matching socket\n");
            return true;
        }

        return (*sockit)->handle_pkt(pkt);
    }
}

void get_dev_stats(dev_stats& stats)
{
    return virtio::shm_xchg::get_instance()->get_stats(stats);
}

void get_queue_stats(unsigned queue, dev_stats& stats)
{
    return virtio::shm_xchg::get_instance()->get_queue_stats(queue, stats);
}

socket::~socket()
{
    // Remove the socket form the sockets map if present. A socket is stored if
    // it is bound to a local port
    if (_id.lport) {
        sockets_mtx.lock();
        auto sockit = sockets.owner_find(this);
        if (sockit) {
            sockets.erase(sockit);
        }
        sockets_mtx.unlock();
    }
}

void socket::bind(uint16_t port)
{
    if (port == 0) {
        throw std::runtime_error("Invalid port");
    }
    if (_id.lport) {
        throw std::runtime_error("Socket already bound");
    }

    _id.lport = port;

    sockets_mtx.lock();
    // Check that the port isn't already used
    if (sockets.owner_find(this)) {
        _id.lport = 0;
        sockets_mtx.unlock();
        throw std::runtime_error("Port already used");
    } else {
        sockets.insert(this);
    }
    sockets_mtx.unlock();
}

void socket::listen()
{
    throw std::runtime_error("Function not implemented yet");
}

std::unique_ptr<socket> socket::accept()
{
    throw std::runtime_error("Function not implemented yet");
}

void socket::connect(const endpoint& dst)
{
    throw std::runtime_error("Function not implemented yet");
}

// Super dummy algorithm
// Could block all other bind/close for a long time if it cannot find a port
void socket::assign_local_port()
{
    int c;
    sockets_mtx.lock();
    for (c = EPHEMERAL_PORTS_COUNT; c > 0; c--) {
        _id.lport = ++last_assigned_port;
        if (!sockets.owner_find(this)) {
            sockets.insert(this);
            sockets_mtx.unlock();
            break;
        }

        // Wrap
        if (last_assigned_port == 0) {
            last_assigned_port = EPHEMERAL_PORTS_FIRST - 1;
        }
    }
    
    _id.lport = 0;
    throw std::runtime_error("Cannot find an available local port\n");
}

bool socket::xmit_desc(const shm_desc& desc, const endpoint& dst)
{
    if (!_id.lport) {
        assign_local_port();
    }

    pkt p = {
        .shm_desc = desc,
        .hdr = {
            .saddr = local_addr,
            .daddr = dst.addr,
            .sport = _id.lport,
            .dport = dst.port,
            .type = _id.type
        }
    };

    return virtio::shm_xchg::get_instance()->xmit_pkt(p);
}

void socket::recv_desc(shm_desc& desc, endpoint& src)
{
    _rx_queue.consume(desc);

    // Need to pass the src in the rx ring to populate this field
}

bool socket::handle_pkt(const pkt& pkt)
{
    return _rx_queue.produce(pkt.shm_desc);
}

}  // net
}  // h2os