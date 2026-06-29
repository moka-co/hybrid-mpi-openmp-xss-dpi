# Top-level Makefile
CC = gcc
CFLAGS = -Wall -O3 -Isrc/
LDFLAGS = 

# Source files
CORE_SRC = src/pattern_matching.c

# Targets
TEST_BIN_1 = tests/test_ac
TEST_BIN_2 = tests/test_ac_file

.PHONY: all clean test test_basic test_file

all: $(TEST_BIN_1) $(TEST_BIN_2)

# Build the basic unit test
$(TEST_BIN_1): tests/test_ac.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^

# Build the file-based capacity test
$(TEST_BIN_2): tests/test_ac_file.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^

# Run specific tests or all of them
test: test_basic test_file

test_basic: $(TEST_BIN_1)
	@echo "Running basic unit test..."
	./$(TEST_BIN_1)

test_file: $(TEST_BIN_2)
	@echo "Running file-based capacity test..."
	./$(TEST_BIN_2)

clean:
	rm -f $(TEST_BIN_1) $(TEST_BIN_2)