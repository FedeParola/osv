/*
 * Copyright (C) 2023 Federico Parola <federico.parola@polito.it>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef H2OS_SOCK_QUEUE_HH
#define H2OS_SOCK_QUEUE_HH

#include <h2os/net.hh>

#ifdef __cplusplus
extern "C" {
#endif

#define SOCK_QUEUE_SIZE 256  // Must be a power of 2
#define SOCK_QUEUE_MASK (SOCK_QUEUE_SIZE - 1)

// MPSC queue
// Producing is never blocking, if the queue is full the operation fails
// Consuming can be blocking on an empty queue (can change behaviour at run time
// or only at init time?)
// Internal implementation is opaque
struct sock_queue;

struct sock_queue *sock_queue_create();
void sock_queue_free(struct sock_queue *q);
int sock_queue_produce(struct sock_queue *q, const struct h2os_shm_desc *desc);
void sock_queue_consume(struct sock_queue *q, struct h2os_shm_desc *desc);

#ifdef __cplusplus
}
#endif

#endif  // H2OS_SOCK_QUEUE_HH