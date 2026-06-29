# Top-level Makefile
CC = gcc
CFLAGS = -Wall -O3 -Isrc/
LDFLAGS = 

# Targets
TEST_BIN = tests/test_ac
SRC = tests/test_ac.c src/pattern_matching.c

.PHONY: all clean test

all: $(TEST_BIN)

$(TEST_BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(TEST_BIN)