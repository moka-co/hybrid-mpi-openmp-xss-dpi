# Top-level Makefile
CC = gcc
CFLAGS = -Wall -O3 -Isrc/
LDFLAGS = -lm

# Source files
CORE_SRC = src/pattern_matching.c src/dataset.c

# Targets
TEST_BIN_1 = tests/test_ac.o
TEST_BIN_2 = tests/test_ac_file.o
BENCH_BIN  = tests/benchmarks/benchmark_ac.o
VALIDATE_BIN = tests/validate_dataset.o

.PHONY: all clean test test_basic test_file benchmark validate

all: $(TEST_BIN_1) $(TEST_BIN_2) $(BENCH_BIN) $(VALIDATE_BIN)

# Build the basic unit test
$(TEST_BIN_1): tests/test_ac.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Build the file-based capacity test
$(TEST_BIN_2): tests/test_ac_file.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Build the performance benchmark
$(BENCH_BIN): tests/benchmarks/benchmark_ac.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Build the dataset validation program
$(VALIDATE_BIN): tests/validate_dataset.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Run specific tests or all of them
test: test_basic test_file

test_basic: $(TEST_BIN_1)
	@echo "Running basic unit test..."
	./$(TEST_BIN_1)

test_file: $(TEST_BIN_2)
	@echo "Running file-based capacity test..."
	./$(TEST_BIN_2)

benchmark: $(BENCH_BIN)
	@echo "Running performance benchmark..."
	./$(BENCH_BIN)

# Run benchmark with custom pattern count (e.g. make benchmark_custom NUM=5000)
benchmark_custom: $(BENCH_BIN)
	@echo "Running performance benchmark with $(NUM) patterns..."
	./$(BENCH_BIN) $(NUM)

validate_dataset: $(VALIDATE_BIN)
	@echo "Running dataset validation..."
	./$(VALIDATE_BIN)

clean:
	rm -f $(TEST_BIN_1) $(TEST_BIN_2) $(BENCH_BIN) $(VALIDATE_BIN) \
	      baseline_benchmark.txt validation_log.txt packets.bin