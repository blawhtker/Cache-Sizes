# Makefile for CompArchProject1
CC := clang
CFLAGS := -O2 -std=c11

BIN := SIM
SRC := SIM.c

.PHONY: all clean example

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $(BIN) $(SRC)

# Quick sanity run on a tiny trace (uses traces/tiny.t)
example: $(BIN)
	./$(BIN) 1024 1 0 1 traces/tiny.t

clean:
	rm -f $(BIN)
