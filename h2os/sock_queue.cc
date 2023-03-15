/*
 * Copyright (C) 2023 Federico Parola <federico.parola@polito.it>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Wait-free MPSC FIFO inspired by https://github.com/dbittman/waitfree-mpsc-queue
// with additional wait operation when consuming on empty queue

#include <atomic>
#include <h2os/sock_queue.hh>
#include <osv/wait_record.hh>

struct sock_queue {
    struct h2os_shm_desc descs[SOCK_QUEUE_SIZE];
    //  1 - descriptor available
    //  0 - descriptor not available
    // -1 - descriptor not available and consumer waiting/preparing to wait
    std::atomic_int8_t desc_available[SOCK_QUEUE_SIZE];
    unsigned long cons_next;      // Next element to be read by the consumer
    std::atomic_ulong prod_next;  // Next element to be written by the producer
    std::atomic_int count;
    std::atomic<waiter *> waitobj;
    std::atomic_bool cancel_wait;
};

struct sock_queue *sock_queue_create()
{
    return new sock_queue();
}

void sock_queue_free(struct sock_queue *q)
{
    delete q;
}

int sock_queue_produce(struct sock_queue *q, const struct h2os_shm_desc *desc)
{
    if (q->count.fetch_add(1) >= SOCK_QUEUE_SIZE) {
        q->count.fetch_sub(1);
        return -1;
    }

    unsigned long to_write = q->prod_next.fetch_add(1);
    q->descs[to_write & SOCK_QUEUE_MASK] = *desc;
    if (q->desc_available[to_write & SOCK_QUEUE_MASK].fetch_add(1) == -1) {
        // The consumer is waiting or preaparing to wait on the item we just
        // produced
        // Propose it to cancel the operation
        q->cancel_wait.store(true);
        waiter *waitobj = q->waitobj.load();
        if (waitobj) {
            // Withdraw the proposal if still available
            bool available = true;
            if (q->cancel_wait.compare_exchange_strong(available, false)) {
                // Proposal withdrawn, it's up to us to wake the consumer
                // The wake is cached even if the consumer is not waiting yet
                waitobj->wake();
            }
            // else the consumer accepted the proposal and canceled wait
        }
        // else the consumer will see the proposal and accept it 
    }

    return 0;
}

void sock_queue_consume(struct sock_queue *q, struct h2os_shm_desc *desc)
{
    if (q->desc_available[q->cons_next & SOCK_QUEUE_MASK].fetch_sub(1) == 0) {
        // The queue is empty, need to wait
        waiter waitobj(sched::thread::current());
        q->waitobj = &waitobj;
        // Check if there is a proposal to cancel wait and try to accept it
        bool available = true;
        if (!q->cancel_wait.compare_exchange_strong(available, false)) {
            // Proposal not available (whithdrawn or never made)
            waitobj.wait();
        }
        q->waitobj = nullptr;
    }

    *desc = q->descs[q->cons_next++ & SOCK_QUEUE_MASK];
    q->count.fetch_sub(1);
}