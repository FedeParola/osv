/*
 * Copyright (C) 2023 Federico Parola <federico.parola@polito.it>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <h2os/sock_queue.hh>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define ERROR(fmt, ...) {						\
	fprintf(stderr, fmt "\n", ##__VA_ARGS__);	\
	exit(EXIT_FAILURE);							\
}

#define SYSERROR(fmt, ...) {										\
	fprintf(stderr, fmt ": %s\n", ##__VA_ARGS__, strerror(errno));	\
	exit(EXIT_FAILURE);												\
}

#define MAX_PRODUCERS 16

static volatile int running = 1;
static volatile unsigned long stat = 0;
static struct sock_queue *q;

static unsigned long consumed;
static unsigned long produced[MAX_PRODUCERS];
static unsigned long produce_errors[MAX_PRODUCERS];
static struct timespec stop;

void sigint_handler(int signum)
{
	if (clock_gettime(CLOCK_MONOTONIC, &stop)) {
		SYSERROR("Error getting time");
	}
	running = 0;
}

static void *consumer(void *unused)
{
	struct h2os_shm_desc desc;
	
	while (running) {
		sock_queue_consume(q, &desc);
		consumed++;
	}

	return NULL;
}

static void *producer(void *arg)
{
	unsigned id = (unsigned)(unsigned long)arg;
	struct h2os_shm_desc desc;
	
	while (running) {
		if (sock_queue_produce(q, &desc)) {
			produce_errors[id]++;
		} else {
			produced[id]++;
		}
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	unsigned nproducers;
	pthread_t consumert, producers[MAX_PRODUCERS];

	if (argc != 2) {
		ERROR("usage: %s <producers>", argv[0]);
	}
	
	nproducers = atoi(argv[1]);

	q = sock_queue_create();
	if (!q) {
		ERROR("Error creating socket queue");
	}

	pthread_t main_t = pthread_self();
	cpu_set_t cpu_set;
	if (pthread_getaffinity_np(main_t, sizeof(cpu_set_t), &cpu_set)) {
		SYSERROR("Error getting CPU affinity list");
	}

	int num_cpus = CPU_COUNT(&cpu_set), curr_cpu = 0;
	if (num_cpus < nproducers + 1) {
		ERROR("At least %d vCPUs are required for this test", nproducers + 1);
	}

	if (pthread_create(&consumert, NULL, consumer, q)) {
		SYSERROR("Error creating consumer thread");
	}
	CPU_ZERO(&cpu_set);
	CPU_SET(curr_cpu++, &cpu_set);
	if (pthread_setaffinity_np(consumert, sizeof(cpu_set_t), &cpu_set)) {
		SYSERROR("Error setting consumer CPU affinity");
	}

	struct timespec start;
	if (clock_gettime(CLOCK_MONOTONIC, &start)) {
		SYSERROR("Error getting time");
	}

	for (int i = 0; i < nproducers; i++) {
		if (pthread_create(&producers[i], NULL, producer,
				(void *)(unsigned long)i)) {
			SYSERROR("Error creating producer thread");
		}

		CPU_ZERO(&cpu_set);
		CPU_SET(curr_cpu++, &cpu_set);
		if (pthread_setaffinity_np(producers[i], sizeof(cpu_set_t), &cpu_set)) {
			SYSERROR("Error setting producer CPU affinity");
		}
	}

	if (signal(SIGINT, sigint_handler) == SIG_ERR) {
		SYSERROR("Error setting SIGINT handler");
	}

	sleep(1);
	unsigned long old_consumed = 0;
	unsigned long tot_produced, old_produced = 0;
	unsigned long tot_produce_errors, old_produce_errors = 0;
	while(running) {
		tot_produced = 0;
		tot_produce_errors = 0;
		for (int i = 0; i < nproducers; i++) {
			tot_produced += produced[i];
			tot_produce_errors += produce_errors[i];
		}

		printf("Last sec: %lu consumed, %lu produced, %lu produce errors\n"
				"Tot: %lu consumed, %lu produced, %lu produce errors\n\n",
				consumed - old_consumed, tot_produced - old_produced,
				tot_produce_errors - old_produce_errors,
				consumed, tot_produced,	tot_produce_errors);
		old_consumed = consumed;
		old_produced = tot_produced;
		old_produce_errors = tot_produce_errors;

		sleep(1);
	}

	double elapsed = (stop.tv_sec - start.tv_sec)
			+ (double)(stop.tv_nsec - start.tv_nsec) / 1000000000;

	tot_produced = 0;
	tot_produce_errors = 0;
	for (int i = 0; i < nproducers; i++) {
		tot_produced += produced[i];
		tot_produce_errors += produce_errors[i];
	}

	printf("GLOBAL AVERAGE: %lu consumed/s, %lu produced/s, "
			"%lu produce errors/s\n", (unsigned long)(consumed/elapsed), 
			(unsigned long)(tot_produced/elapsed),
			(unsigned long)(tot_produce_errors/elapsed));

	for (int i = 0; i < nproducers; i++) {
		if (pthread_join(producers[i], NULL)) {
			SYSERROR("Error joining producer thread");
		}
	}

	if (pthread_join(consumert, NULL)) {
		SYSERROR("Error joining consumer thread");
	}

	sock_queue_free(q);

	return EXIT_SUCCESS;
}
