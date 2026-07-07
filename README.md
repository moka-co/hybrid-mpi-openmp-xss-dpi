# Hybrid MPI+OpenMP XSS DPI Detector

This project implements a high-performance Deep Packet Inspection (DPI) system for detecting Cross-Site Scripting (XSS) vulnerabilities. Inspired by intrusion detection systems like Suricata, this project focuses on scaling and parallelizing pattern-matching algorithms to handle high-throughput, heterogeneous network traffic.

The core objective is to achieve efficient parallelization of the Aho-Corasick string-matching algorithm. By distributing heterogeneous packet processing across computing nodes using MPI and multi-threading with OpenMP, we aim to optimize packet inspection throughput. This system investigates the performance impact of different scheduling strategies (static vs. dynamic) in the context of load balancing.

## Technical Stack
- **Languages:** C
- **Parallelism:** Open MPI (Process-level distribution), OpenMP (Thread-level parallelization)
- **Algorithms:** Aho-Corasick (Pattern Matching)
- **Infrastructure:** AWS (slurm clusters), SLURM (Job submission)
- **Analysis:** Python (Data plotting)

## Repository Structure
```
./
├── README.md
├── Makefile
├── scripts/
│   ├── benchmark.slurm
│   ├── example_benchmark.slurm
│   ├── run_benchmark_packet_size_comparison.py
│   ├── run_experiments.py
│   └── submit_jobs.sh
├── src/
│   ├── config.c
│   ├── config.h
│   ├── dataset.c
│   ├── dataset.h
│   ├── dpi_engine.c
│   ├── pattern_matching.c
│   ├── pattern_matching.h
│   ├── performance.c
│   └── performance.h
├── analysis/
│   ├── plot.py
│   ├── plot_benchmark.py
│   └── plots/
├── results/
└── tests/
```

## Building
The project uses a standard Makefile. To build the executables and run initial tests:
```bash
make clean
make all
```

## Running
For benchmark tests, use the following commands:
```bash
make benchmark
make benchmark_t NUM_THREADS=4
make benchmark_p NUM_PROCESS=2
make benchmark_pt NUM_PROCESS=2 NUM_THREADS=4
```

You can see useful statistics about the XSS detection by running:
```bash
make validate
```

## Experimental Reproductions
### Local
Experiments can be run automatically using the provided Python script:
```bash
python3 scripts/run_experiments.py
```
To run the automated scaling benchmarks and generate a plot (requires `matplotlib`):
```bash
python3 scripts/run_benchmark_packet_size_comparison.py
python3 analysis/plot_benchmark.py
```
Results are saved to the `results/` directory, and the plot is saved under `analysis/plots/`.

### SLURM Cluster
For cluster environments, use the provided Bash scripts to submit batch jobs to the scheduler:
```bash
sbatch scripts/submit_jobs.sh
```

## Generating Plots
After running experiments and collecting results in the `results/` directory, you can generate various plots using the scripts in the `analysis/` directory (requires `matplotlib` and `pandas`):

### General Performance Plots
```bash
cd analysis
python3 plot.py
```

### Thread Imbalance Analysis
To visualize the workload distribution between threads using static vs. dynamic scheduling:
```bash
python3 analysis/plot_thread_imbalance_comparison.py
```

### Benchmark Comparison
To visualize throughput based on packet counts:
```bash
python3 analysis/plot_benchmark_comparison.py
```

All output plots will be saved under the `plots/` directory.
