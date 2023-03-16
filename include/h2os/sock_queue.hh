/*
 * Copyright (C) 2023 Federico Parola <federico.parola@polito.it>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef H2OS_SOCK_QUEUE_HH
#define H2OS_SOCK_QUEUE_HH

#include <atomic>
#include <h2os/net.hh>
#include <osv/wait_record.hh>

namespace h2os {
namespace net {

// Could be defined as a nested class of the socket in the future, I'm keeping
// it as a standalone entity to test it
class sock_queue {
public:
    sock_queue() {};
    sock_queue(sock_queue&) = delete;
    bool produce(const shm_desc& desc);
    void consume(shm_desc& desc);

private:
    static const int SIZE = 256;
    static const unsigned long MASK = SIZE - 1;
    shm_desc _descs[SIZE];
    //  1 - descriptor available
    //  0 - descriptor not available
    // -1 - descriptor not available and consumer waiting/preparing to wait
    std::atomic_int8_t _desc_available[SIZE];
    unsigned long _cons_next;      // Next element to be read by the consumer
    std::atomic_ulong _prod_next;  // Next element to be written by the producer
    std::atomic_int _count;
    std::atomic<waiter *> _waitobj;
    std::atomic_bool _cancel_wait;
};

}  // net
}  // h2os

#endif  // H2OS_SOCK_QUEUE_HH