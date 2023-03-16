/*
 * Copyright (C) 2023 Federico Parola <federico.parola@polito.it>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef H2OS_SOCKET_HH
#define H2OS_SOCKET_HH

#include <cstring>
#include <h2os/sock_queue.hh>

namespace h2os {
namespace net {

class socket {
public:
    struct id {
        // Following two fields are 0 if socket is not connected
        uint32_t raddr;
        uint16_t rport;
        uint16_t lport;  // 0 if socket not bound
        socket_type type;

        struct hash {
            std::size_t operator()(const id& id) const noexcept {
                std::size_t h1 = std::hash<uint32_t>{}(id.raddr);
                std::size_t h2 = std::hash<uint16_t>{}(id.rport);
                std::size_t h3 = std::hash<uint16_t>{}(id.lport);
                std::size_t h4 = std::hash<socket_type>{}(id.type);
                // Combine hash as suggested by the cppreference:
                // https://en.cppreference.com/w/cpp/utility/hash#Example
                return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
            }
        };

        struct equal {
            bool operator()(const struct id& id, const socket *s)
                    const noexcept {
                return !std::memcmp(&id, &s->_id, sizeof(id));
            }
        };
    };

    socket(socket_type type): _id{.type = type} {};
    socket(socket&) = delete;
    ~socket();
    id get_id() const { return _id; };
    void bind(uint16_t port);
    void listen();
    std::unique_ptr<socket> accept();
    void connect(const endpoint& dst);
    bool xmit_desc(const shm_desc& desc, const endpoint& dst);
    void recv_desc(shm_desc& desc, endpoint& src);
    bool handle_pkt(const pkt& pkt);

private:
    void assign_local_port();

    id _id;
    sock_queue _rx_queue;
};

}  // net
}  // h2os

#endif  // H2OS_SOCKET_HH