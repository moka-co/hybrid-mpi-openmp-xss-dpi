# Top-level Makefile
CC = mpicc
CFLAGS = -Wall -O3 -fopenmp -Isrc/ -DHAVE_MPI
LDFLAGS = -fopenmp -lm

# Source files
CORE_SRC = src/pattern_matching.c src/dataset.c src/config.c
MAIN_SRC = src/dpi_engine.c

# Dataset used by the run* targets below. Override on the command line to
# swap datasets without touching the Makefile, e.g.:
#   make run24 DATASET=datasets/csic_get_post.txt
DATASET = datasets/packets.txt

# Number of threads for multithreaded benchmarks
NUM_THREADS = 4
NUM_PROCESS = 2

# Targets
MAIN_BIN     = dpi_engine.o
TEST_BIN_1   = tests/test_ac.o
TEST_BIN_2   = tests/test_ac_file.o
TEST_CONFIG  = tests/test_config.o
BENCH_BIN    = tests/benchmarks/benchmark_ac.o
BENCH_BIN_T  = tests/benchmarks/benchmark_ac_t.o
BENCH_BIN_P  = tests/benchmarks/benchmark_ac_p.o
BENCH_BIN_PT = tests/benchmarks/benchmark_ac_pt.o
VALIDATE_BIN = tests/validate_dataset.o
CSIC_BIN     = tests/generate_csic_dataset.o

.PHONY: all clean test test_basic test_file test_config benchmark benchmark_t benchmark_p benchmark_pt validate csic_dataset run

all: $(MAIN_BIN) $(TEST_BIN_1) $(TEST_BIN_2) $(TEST_CONFIG) $(BENCH_BIN) $(BENCH_BIN_T) $(BENCH_BIN_P) $(BENCH_BIN_PT) $(VALIDATE_BIN) $(CSIC_BIN)

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

# Build the multithreaded performance benchmark
$(BENCH_BIN_T): tests/benchmarks/benchmark_ac_t.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Build the MPI performance benchmark
$(BENCH_BIN_P): tests/benchmarks/benchmark_ac_p.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Build the hybrid MPI+OpenMP performance benchmark
$(BENCH_BIN_PT): tests/benchmarks/benchmark_ac_pt.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Build the dataset validation program
$(VALIDATE_BIN): tests/validate_dataset.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Build the real-data (CSIC 2010) dataset validation program
$(CSIC_BIN): tests/generate_csic_dataset.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Run the engine using the generated dataset layout
# Override the dataset with: make run24 DATASET=datasets/other.bin
run24: $(MAIN_BIN)
	@echo "Launching DPI engine across hybrid MPI cluster layout..."
	mpirun -np 2 ./$(MAIN_BIN) --omp-threads 4 --schedule dynamic,16 --pattern-file datasets/patterns.txt --dataset-file $(DATASET)

run28: $(MAIN_BIN)
	@echo "Launching DPI engine across hybrid MPI cluster layout..."
	mpirun -np 2 ./$(MAIN_BIN) --omp-threads 8 --schedule dynamic,16 --pattern-file datasets/patterns.txt --dataset-file $(DATASET)

run42: $(MAIN_BIN)
	@echo "Launching DPI engine across hybrid MPI cluster layout..."
	mpirun -np 3 ./$(MAIN_BIN) --omp-threads 2 --schedule dynamic,16 --pattern-file datasets/patterns.txt --dataset-file $(DATASET)

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

# Usage: make csic_dataset ARGS="datasets-private/csic_get_post.txt"
csic_dataset: $(CSIC_BIN)
	@echo "Running CSIC 2010 real-data validation..."
	./$(CSIC_BIN) $(ARGS)

clean:
	rm -f $(MAIN_BIN) $(TEST_BIN_1) $(TEST_BIN_2) $(TEST_CONFIG) $(BENCH_BIN) $(BENCH_BIN_T) $(BENCH_BIN_P) $(BENCH_BIN_PT) $(VALIDATE_BIN) $(CSIC_BIN)
	rm -f *.o src/*.o tests/*.o tests/benchmarks/*.o
