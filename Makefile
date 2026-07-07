# Top-level Makefile
CC = mpicc
CFLAGS = -Wall -O3 -fopenmp -Isrc/ -DHAVE_MPI
LDFLAGS = -fopenmp -lm

# Source files
CORE_SRC = src/pattern_matching.c src/dataset.c src/config.c src/performance.c

# Dataset used by the run* targets below.
DATASET = datasets/packets.txt
PATTERNS = datasets/patterns.txt

# Number of threads for multithreaded benchmarks
NUM_THREADS = 4
NUM_PROCESS = 2
PACKETS = 1000000

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

.PHONY: all clean test test_basic test_file test_config benchmark benchmark_t benchmark_p benchmark_pt validate csic_dataset

all: $(TEST_BIN_1) $(TEST_BIN_2) $(TEST_CONFIG) $(BENCH_BIN) $(BENCH_BIN_T) $(BENCH_BIN_P) $(BENCH_BIN_PT) $(VALIDATE_BIN) $(CSIC_BIN)

# Build rules
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
	@echo "Running performance benchmark with $(PACKETS) packets..."
	./$(BENCH_BIN) --num-packets $(PACKETS)

benchmark_t: $(BENCH_BIN_T)
	@echo "Running multithreaded performance benchmark with $(THREADS) threads and $(PACKETS) packets..."
	OMP_NUM_THREADS=$(NUM_THREADS) ./$(BENCH_BIN_T) --num-packets $(PACKETS) --omp-threads $(THREADS)

benchmark_p: $(BENCH_BIN_P)
	@echo "Running MPI performance benchmark with $(NP) ranks and $(PACKETS) packets..."
	mpirun -np $(NP) ./$(BENCH_BIN_P) --num-packets $(PACKETS)

benchmark_pt: $(BENCH_BIN_PT)
	@echo "Running Hybrid MPI+OpenMP performance benchmark with $(NP) processes, $(NUM_THREADS) threads per rank, and $(PACKETS) packets..."
	mpirun -np $(NP) -x OMP_NUM_THREADS=$(NUM_THREADS) ./$(BENCH_BIN_PT) --num-packets $(PACKETS) --omp-threads $(NUM_THREADS)


validate: $(VALIDATE_BIN)
	@echo "Running dataset validation program..."
	./$(VALIDATE_BIN)

csic_dataset: $(CSIC_BIN)
	@echo "Running CSIC 2010 real-data validation..."
	./$(CSIC_BIN) $(ARGS)

clean:
	rm -f $(TEST_BIN_1) $(TEST_BIN_2) $(TEST_CONFIG) $(BENCH_BIN) $(BENCH_BIN_T) $(BENCH_BIN_P) $(BENCH_BIN_PT) $(VALIDATE_BIN) $(CSIC_BIN)
	rm -f src/*.o tests/*.o tests/benchmarks/*.o
