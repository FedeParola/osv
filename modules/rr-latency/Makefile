.PHONY: module
module: rr-latency

CFLAGS = -std=gnu99 -fpie -rdynamic

rr-latency: main.c
	$(CC) -pie -o $@ $(CFLAGS) $(LDFLAGS)main.c

clean:
	rm -f rr-latency
