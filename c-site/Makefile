CC     ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

server: server.c
	$(CC) $(CFLAGS) -o $@ $<

run: server
	./server 8080

clean:
	rm -f server

.PHONY: run clean
