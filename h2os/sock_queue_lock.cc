/*
 * Copyright (C) 2023 Federico Parola <federico.parola@polito.it>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Producing always requires a lock (multiple producers), consuming requires a
// lock only when there are no elements in the queue and the thread needs to
// wait (guarantees no wakeups are missed)

#include <atomic>
#include <h2os/sock_queue.hh>
#include <osv/waitqueue.hh>
#include <osv/mutex.h>

struct sock_queue {
    struct h2os_shm_desc descs[SOCK_QUEUE_SIZE];
    std::atomic_ulong cons_next;  // Next element to be read by the consumer
    std::atomic_ulong prod_next;  // Next element to be written by the producer
    mutex m;
    waitqueue wq;
};

struct sock_queue *sock_queue_create()
{
    return new struct sock_queue();
}

void sock_queue_free(struct sock_queue *q)
{
    delete q;
}

int sock_queue_produce(struct sock_queue *q, const struct h2os_shm_desc *desc)
{
    int ret = 0;

    q->m.lock();

    unsigned long local_prod_next = q->prod_next.load();
    if (local_prod_next - q->cons_next == SOCK_QUEUE_SIZE) {
        ret = -1;
        goto out;
    }

    q->descs[local_prod_next & SOCK_QUEUE_MASK] = *desc;
    q->prod_next.store(local_prod_next + 1);
    q->wq.wake_one(q->m);
    
out:
    q->m.unlock();
    return ret;
}

void sock_queue_consume(struct sock_queue *q, struct h2os_shm_desc *desc)
{
    unsigned long local_cons_next = q->cons_next.load();

    if (local_cons_next == q->prod_next) {
        q->m.lock();
        while (local_cons_next == q->prod_next) {
            q->wq.wait(q->m);
        }
        q->m.unlock();
    }

    *desc = q->descs[local_cons_next & SOCK_QUEUE_MASK];
    q->cons_next.store(local_cons_next + 1);
}