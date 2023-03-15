.PHONY: module
module: ring_test.so

CFLAGS = -std=gnu99 -fPIC -shared -I../../include

ring_test.so: ring_test.c
	$(CC) -pie -o $@ $(CFLAGS) $(LDFLAGS) ring_test.c

clean:
	rm -f ring_test.so
