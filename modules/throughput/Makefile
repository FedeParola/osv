.PHONY: module
module: throughput

CFLAGS = -std=gnu99 -fpie -rdynamic

throughput: main.c
	$(CC) -pie -o $@ $(CFLAGS) $(LDFLAGS) main.c

clean:
	rm -f throughput
