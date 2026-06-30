# Top-level Makefile
CC = gcc
CFLAGS = -Wall -O3 -Isrc/
LDFLAGS = -lm

# Source files
CORE_SRC = src/pattern_matching.c src/dataset.c src/config.c

# Targets
TEST_BIN_1   = tests/test_ac.o
TEST_BIN_2   = tests/test_ac_file.o
TEST_CONFIG  = tests/test_config.o
BENCH_BIN    = tests/benchmarks/benchmark_ac.o
VALIDATE_BIN = tests/validate_dataset.o

.PHONY: all clean test test_basic test_file test_config benchmark validate

all: $(TEST_BIN_1) $(TEST_BIN_2) $(TEST_CONFIG) $(BENCH_BIN) $(VALIDATE_BIN)

# Build the basic unit test
$(TEST_BIN_1): tests/test_ac.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Build the file-based capacity test
$(TEST_BIN_2): tests/test_ac_file.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Build the configuration and CLI parser unit test
$(TEST_CONFIG): tests/test_config.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Build the performance benchmark
$(BENCH_BIN): tests/benchmarks/benchmark_ac.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Build the dataset validation program
$(VALIDATE_BIN): tests/validate_dataset.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Run specific tests or all of them
test: test_basic test_file test_config

test_basic: $(TEST_BIN_1)
	@echo "Running basic unit test..."
	./$(TEST_BIN_1)

test_file: $(TEST_BIN_2)
	@echo "Running file-based capacity test..."
	./$(TEST_BIN_2)

test_config: $(TEST_CONFIG)
	@echo "Running configuration suite unit tests..."
	./$(TEST_CONFIG)

benchmark: $(BENCH_BIN)
	@echo "Running performance benchmark..."
	./$(BENCH_BIN)

validate: $(VALIDATE_BIN)
	@echo "Running dataset validation program..."
	./$(VALIDATE_BIN)

clean:
	rm -f $(TEST_BIN_1) $(TEST_BIN_2) $(TEST_CONFIG) $(BENCH_BIN) $(VALIDATE_BIN)
	rm -f *.o src/*.o tests/*.o