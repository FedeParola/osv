/*
 * Copyright (C) 2023 Federico Parola <federico.parola@polito.it>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <h2os/net.hh>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define ERROR(fmt, ...) {						\
	fprintf(stderr, fmt "\n", ##__VA_ARGS__);	\
	exit(EXIT_FAILURE);							\
}

#define SYSERROR(fmt, ...) {										\
	fprintf(stderr, fmt ": %s\n", ##__VA_ARGS__, strerror(errno));	\
	exit(EXIT_FAILURE);												\
}

#define PING_PONG_PORT 5000

volatile int running = 1;
volatile unsigned long stat = 0;

void sigint_handler(int signum)
{
	running = 0;
}

static void *send_descs(void *arg)
{
	int i = 0;
	struct h2os_shm_desc desc;
	struct h2os_endpoint dst = {0};
	struct h2os_socket *s = (struct h2os_socket *)arg;

	while (running) {
		desc.addr = i;
		desc.len = 0;
		if (h2os_xmit_desc(s, &desc, &dst)) {
			fprintf(stderr, "Error sending descriptor\n");
		} else {
			printf("Sent descriptor with addr=0x%lx and len=0\n",
					(uint64_t)i++);
		}
		sleep(1);
	}

	return NULL;
}

static void *recv_descs(void *arg)
{
	struct h2os_shm_desc desc;
	struct h2os_socket *s = (struct h2os_socket *)arg;

	if (h2os_socket_bind(s, PING_PONG_PORT)) {
		ERROR("Error binding socket to port %d\n", PING_PONG_PORT);
	}

	while (running) {
		h2os_recv_desc(s, &desc, NULL);
		stat++;
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	int ret;

	int action_send = 0;

	if (argc > 2) {
		ERROR("usage: %s [-s]", argv[0]);
	} else if (argc == 2) {
		if (strcmp(argv[1], "-s")) {
			ERROR("usage: %s [-s]", argv[0]);
		}
		action_send = 1;
	}

	if (signal(SIGINT, sigint_handler) == SIG_ERR) {
		SYSERROR("Error setting SIGINT handler");
	}

	struct h2os_socket *s = h2os_socket_open();
	if (!s) {
		ERROR("Error creating H2OS socket");
	}

	pthread_t t;
	if (pthread_create(&t, NULL, action_send ? send_descs : recv_descs, s)) {
		SYSERROR("Error creating thread");
	}

	sleep(1);

	struct h2os_dev_stats devstats, old_devstats = {0};
	unsigned long old_stat = 0;
	while(running) {
		h2os_get_dev_stats(&devstats);
		printf("Last sec: %lu apprx, %lu devrx, %lu sockq_full, %lu wakeups\n"
				"Tot: %lu apprx, %lu devrx, %lu sockq_full, %lu wakeups\n\n",
				stat - old_stat, devstats.rx_pkts - old_devstats.rx_pkts,
				devstats.rx_sockq_full - old_devstats.rx_sockq_full,
				devstats.rx_wakeups - old_devstats.rx_wakeups, stat,
				devstats.rx_pkts, devstats.rx_sockq_full, devstats.rx_wakeups);
		old_stat = stat;
		old_devstats = devstats;

		sleep(1);
	}

	if (pthread_join(t, NULL)) {
		SYSERROR("Error joining thread");
	}

	h2os_socket_close(s);

	return EXIT_SUCCESS;
}
