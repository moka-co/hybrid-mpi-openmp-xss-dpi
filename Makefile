# Top-level Makefile
CC = mpicc
CFLAGS = -Wall -O3 -fopenmp -Isrc/ -DHAVE_MPI
LDFLAGS = -fopenmp -lm

# Source files
CORE_SRC = src/pattern_matching.c src/dataset.c src/config.c
MAIN_SRC = src/dpi_engine.c

# Targets
MAIN_BIN     = dpi_engine.o
TEST_BIN_1   = tests/test_ac.o
TEST_BIN_2   = tests/test_ac_file.o
TEST_CONFIG  = tests/test_config.o
BENCH_BIN    = tests/benchmarks/benchmark_ac.o
VALIDATE_BIN = tests/validate_dataset.o

.PHONY: all clean test test_basic test_file test_config benchmark validate run

all: $(MAIN_BIN) $(TEST_BIN_1) $(TEST_BIN_2) $(TEST_CONFIG) $(BENCH_BIN) $(VALIDATE_BIN)

# Build the main DPI hybrid engine
$(MAIN_BIN): $(MAIN_SRC) $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

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

# Run the engine using the generated dataset layout
run24: $(MAIN_BIN)
	@echo "Launching DPI engine across hybrid MPI cluster layout..."
	mpirun -np 2 ./$(MAIN_BIN) --omp-threads 4 --schedule dynamic,16 --pattern-file datasets/patterns.txt

run28: $(MAIN_BIN)
	@echo "Launching DPI engine across hybrid MPI cluster layout..."
	mpirun -np 2 ./$(MAIN_BIN) --omp-threads 8 --schedule dynamic,16 --pattern-file datasets/patterns.txt

run42: $(MAIN_BIN)
	@echo "Launching DPI engine across hybrid MPI cluster layout..."
	mpirun -np 3 ./$(MAIN_BIN) --omp-threads 2 --schedule dynamic,16 --pattern-file datasets/patterns.txt

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
	rm -f $(MAIN_BIN) $(TEST_BIN_1) $(TEST_BIN_2) $(TEST_CONFIG) $(BENCH_BIN) $(VALIDATE_BIN)
	rm -f *.o src/*.o tests/*.o tests/benchmarks/*.o