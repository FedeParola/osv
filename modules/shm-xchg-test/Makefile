.PHONY: module
module: shm_xchg_test.so

CFLAGS = -std=gnu99 -fPIC -shared -I../../include

shm_xchg_test.so: shm_xchg_test.c
	$(CC) -pie -o $@ $(CFLAGS) $(LDFLAGS) shm_xchg_test.c

clean:
	rm -f shm_xchg_test.so
