/*
 * Copyright (C) 2023 Federico Parola <federico.parola@polito.it>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define SHMEM_DEV_PATH "/dev/ivshmem0"
#define SHM_PORT 5100

volatile int stop = 0;

void sigint_handler(int signum)
{
	stop = 1;
}

int main(int argc, char *argv[])
{
	int first = 0;
	struct in_addr remote_addr = {0};
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-f")) {
			first = 1;
			printf("First sending\n");
		} else {
			if (!inet_aton(argv[i], &remote_addr)) {
				fprintf(stderr, "Invalid address %s\n", argv[i]);
				exit(EXIT_FAILURE);
			}
		}
	}

	if (!remote_addr.s_addr) {
		fprintf(stderr, "usage: %s <remote_addr> -f\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	int shm = open("/dev/ivshmem0", O_RDWR);
	if (shm < 0) {
		fprintf(stderr, "Error opening shmem: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	void *shm_addr;
	if (ioctl(shm, 0, &shm_addr)) {
		fprintf(stderr, "Error retrieving shmem address: %s\n",
				strerror(errno));
		exit(EXIT_FAILURE);
	}

	printf("Received addr %p from ioctl()\n", shm_addr);

	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == -1) {
		fprintf(stderr, "Error creating UDP socket: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in si_local = {0}, si_remote = {0};

	si_local.sin_family = AF_INET;
	si_local.sin_port = htons(SHM_PORT);
	si_local.sin_addr.s_addr = htonl(INADDR_ANY);

	si_remote.sin_family = AF_INET;
	si_remote.sin_port = htons(SHM_PORT);
	si_remote.sin_addr = remote_addr;

	if (bind(sock, (struct sockaddr *)&si_local, sizeof(si_local)) == -1) {
		fprintf(stderr, "Error binding UDP socket: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	if (signal(SIGINT, sigint_handler) == SIG_ERR) {
		fprintf(stderr, "Error setting SIGINT handler: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	int message = 0;
	int *shm_int = (int *)shm_addr;

	if (first) {
		*shm_int = 0;

		printf("Sending value %d\n", *shm_int);
		if (sendto(sock, &message, sizeof(message), 0,
				(struct sockaddr *)&si_remote, sizeof(si_remote)) == -1) {
			fprintf(stderr, "Error sending message: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		printf("Value sent\n");
	}

	while(!stop) {
		int rlen;

		printf("Waiting for a value\n");
		if (recvfrom(sock, &message, sizeof(message), 0, NULL, 0) == -1) {
			fprintf(stderr, "Error sending message: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		printf("Received value %d\n", *shm_int);

		(*shm_int)++;

		sleep(1);

		printf("Sending value %d\n", *shm_int);
		if (sendto(sock, &message, sizeof(message), 0,
				(struct sockaddr *)&si_remote, sizeof(si_remote)) == -1) {
			fprintf(stderr, "Error sending message: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		printf("Value sent\n");
	}

	close(sock);
	return EXIT_SUCCESS;
}
