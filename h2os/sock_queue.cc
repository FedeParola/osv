/*
 * Copyright (C) 2023 Federico Parola <federico.parola@polito.it>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Wait-free MPSC FIFO inspired by https://github.com/dbittman/waitfree-mpsc-queue
// with additional wait operation when consuming on empty queue

#include <h2os/sock_queue.hh>

namespace h2os {
namespace net {

bool sock_queue::produce(const shm_desc& desc)
{
    if (_count.fetch_add(1) >= SIZE) {
        _count.fetch_sub(1);
        return true;
    }

    unsigned long to_write = _prod_next.fetch_add(1);
    _descs[to_write & MASK] = desc;
    if (_desc_available[to_write & MASK].fetch_add(1) == -1) {
        // The consumer is waiting or preaparing to wait on the item we just
        // produced
        // Propose it to cancel the operation
        _cancel_wait.store(true);
        waiter *waitobj = _waitobj.load();
        if (waitobj) {
            // Withdraw the proposal if still available
            bool available = true;
            if (_cancel_wait.compare_exchange_strong(available, false)) {
                // Proposal withdrawn, it's up to us to wake the consumer
                // The wake is cached even if the consumer is not waiting yet
                waitobj->wake();
            }
            // else the consumer accepted the proposal and canceled wait
        }
        // else the consumer will see the proposal and accept it 
    }

    return false;
}

void sock_queue::consume(shm_desc& desc)
{
    if (_desc_available[_cons_next & MASK].fetch_sub(1) == 0) {
        // The queue is empty, need to wait
        waiter waitobj(sched::thread::current());
        _waitobj = &waitobj;
        // Check if there is a proposal to cancel wait and try to accept it
        bool available = true;
        if (!_cancel_wait.compare_exchange_strong(available, false)) {
            // Proposal not available (whithdrawn or never made)
            waitobj.wait();
        }
        _waitobj = nullptr;
    }

    desc = _descs[_cons_next++ & MASK];
    _count.fetch_sub(1);
}

}  // net
}  // h2os