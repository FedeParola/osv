#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

int main()
{
	int shm = open("/dev/ivshmem0", O_RDWR);
	if (shm < 0) {
		fprintf(stderr, "Error opening shmem: %s\n", strerror(errno));
	}

	void *shm_addr;
	if (ioctl(shm, 0, &shm_addr)) {
		fprintf(stderr, "Error retrieving shmem address: %s\n",
				strerror(errno));
		exit(EXIT_FAILURE);
	}

	printf("Received addr %p from ioctl()\n", shm_addr);

	char msg[] = "Hello, world!\n";
	memcpy(shm_addr, msg, sizeof(msg));

	printf("Message written to shared memory\n");

	return EXIT_SUCCESS;
}
