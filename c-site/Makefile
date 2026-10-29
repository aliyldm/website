CC     ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CFLAGS += -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -pthread
LDFLAGS += -pthread

SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)

server: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

# Bellek hatalarini ve tanimsiz davranisi yakalamak icin
debug: $(SRC) $(HDR)
	$(CC) -g -O1 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
		-pthread -fsanitize=address,undefined -fno-omit-frame-pointer \
		-o server-debug $(SRC) -pthread

run: server
	./server 8080

clean:
	rm -f server server-debug

.PHONY: run clean debug
