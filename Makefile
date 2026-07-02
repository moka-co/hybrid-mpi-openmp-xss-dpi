# Top-level Makefile
CC = mpicc
CFLAGS = -Wall -O3 -fopenmp -Isrc/ -DHAVE_MPI
LDFLAGS = -fopenmp -lm

# Source files
CORE_SRC = src/pattern_matching.c src/dataset.c src/config.c src/performance.c
MAIN_SRC = src/dpi_engine.c
# Corrected: Binary names should not have .o extension
MAIN_BIN = dpi_engine

# Dataset used by the run* targets below.
DATASET = datasets/packets.txt
PATTERNS = datasets/patterns.txt

# Number of threads for multithreaded benchmarks
NUM_THREADS = 4
NUM_PROCESS = 2

# Run configuration variables
STRATEGY = all
THREADS = 4
SCHEDULE = dynamic,16
NP = 2

# Corrected: Binary names
TEST_BIN_1   = tests/test_ac
TEST_BIN_2   = tests/test_ac_file
TEST_CONFIG  = tests/test_config
BENCH_BIN    = tests/benchmarks/benchmark_ac
BENCH_BIN_T  = tests/benchmarks/benchmark_ac_t
BENCH_BIN_P  = tests/benchmarks/benchmark_ac_p
BENCH_BIN_PT = tests/benchmarks/benchmark_ac_pt
VALIDATE_BIN = tests/validate_dataset
CSIC_BIN     = tests/generate_csic_dataset

.PHONY: all clean test test_basic test_file test_config benchmark benchmark_t benchmark_p benchmark_pt validate csic_dataset run

all: $(MAIN_BIN) $(TEST_BIN_1) $(TEST_BIN_2) $(TEST_CONFIG) $(BENCH_BIN) $(BENCH_BIN_T) $(BENCH_BIN_P) $(BENCH_BIN_PT) $(VALIDATE_BIN) $(CSIC_BIN)

# Build rules
$(MAIN_BIN): $(MAIN_SRC) $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_BIN_1): tests/test_ac.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_BIN_2): tests/test_ac_file.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_CONFIG): tests/test_config.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BENCH_BIN): tests/benchmarks/benchmark_ac.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BENCH_BIN_T): tests/benchmarks/benchmark_ac_t.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BENCH_BIN_P): tests/benchmarks/benchmark_ac_p.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BENCH_BIN_PT): tests/benchmarks/benchmark_ac_pt.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(VALIDATE_BIN): tests/validate_dataset.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CSIC_BIN): tests/generate_csic_dataset.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)


# Run the engine
run: $(MAIN_BIN)
	@echo "Launching DPI engine..."
	mpirun -np $(NP) ./$(MAIN_BIN) \
		--strategy $(STRATEGY) \
		--omp-threads $(THREADS) \
		--schedule $(SCHEDULE) \
		--pattern-file $(PATTERNS) \
		--dataset-file $(DATASET)


run24: $(MAIN_BIN)
	@echo "Launching DPI engine across hybrid MPI cluster layout..."
	mpirun -np 2 ./$(MAIN_BIN) --strategy hybrid --omp-threads 4 --schedule dynamic,16 --pattern-file datasets/patterns.txt --dataset-file $(DATASET)

run28: $(MAIN_BIN)
	@echo "Launching DPI engine across hybrid MPI cluster layout..."
	mpirun -np 2 ./$(MAIN_BIN) --strategy hybrid --omp-threads 8 --schedule dynamic,16 --pattern-file datasets/patterns.txt --dataset-file $(DATASET)

run42: $(MAIN_BIN)
	@echo "Launching DPI engine across hybrid MPI cluster layout..."
	mpirun -np 3 ./$(MAIN_BIN) --strategy hybrid --omp-threads 2 --schedule dynamic,16 --pattern-file datasets/patterns.txt --dataset-file $(DATASET)

# Test targets
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

# Benchmark targets
benchmark: $(BENCH_BIN)
	@echo "Running performance benchmark..."
	./$(BENCH_BIN)

benchmark_t: $(BENCH_BIN_T)
	@echo "Running multithreaded performance benchmark with $(NUM_THREADS) threads..."
	OMP_NUM_THREADS=$(NUM_THREADS) ./$(BENCH_BIN_T)

benchmark_p: $(BENCH_BIN_P)
	@echo "Running MPI performance benchmark..."
	mpirun -np $(NUM_PROCESS) ./$(BENCH_BIN_P)

benchmark_pt: $(BENCH_BIN_PT)
	@echo "Running Hybrid MPI+OpenMP performance benchmark with $(NUM_PROCESS) processes and $(NUM_THREADS) threads per rank..."
	mpirun -np $(NUM_PROCESS) -x OMP_NUM_THREADS=$(NUM_THREADS) ./$(BENCH_BIN_PT)


validate: $(VALIDATE_BIN)
	@echo "Running dataset validation program..."
	./$(VALIDATE_BIN)

csic_dataset: $(CSIC_BIN)
	@echo "Running CSIC 2010 real-data validation..."
	./$(CSIC_BIN) $(ARGS)

clean:
	rm -f $(MAIN_BIN) $(TEST_BIN_1) $(TEST_BIN_2) $(TEST_CONFIG) $(BENCH_BIN) $(BENCH_BIN_T) $(BENCH_BIN_P) $(BENCH_BIN_PT) $(VALIDATE_BIN) $(CSIC_BIN)
	rm -f src/*.o tests/*.o tests/benchmarks/*.o
