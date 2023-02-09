.PHONY: module
module: ivshmem_test

CFLAGS = -std=gnu99 -fpie -rdynamic

ivshmem_test: ivshmem_test.c
	$(CC) -pie -o $@ $(CFLAGS) $(LDFLAGS) ivshmem_test.c

clean:
	rm -f ivshmem_test
