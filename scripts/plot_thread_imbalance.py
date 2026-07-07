import subprocess
import os
import matplotlib.pyplot as plt
import re

def run_benchmark():
    print("Compiling benchmark...")
    # Compile
    cmd_compile = ["gcc", "-O3", "-Wall", "-fopenmp", "-Isrc/", "-o", "tests/benchmarks/benchmark_ac_t_logged", "tests/benchmarks/benchmark_ac_t_logged.c", "src/pattern_matching.c", "src/dataset.c", "src/config.c", "-lm"]
    subprocess.run(cmd_compile, check=True)
    
    print("Running benchmark with 8 threads...")
    # Run
    env = os.environ.copy()
    env["OMP_NUM_THREADS"] = "8"
    # Note: Running from the root directory
    cmd_run = ["./tests/benchmarks/benchmark_ac_t_logged", "--num-packets", "1000000", "--omp-threads", "8"]
    subprocess.run(cmd_run, env=env, check=True)

def parse_logs():
    data = {}
    if not os.path.exists("results/thread_imbalance.log"):
        print("Error: Log file not found.")
        return data
        
    with open("results/thread_imbalance.log", "r") as f:
        for line in f:
            match = re.search(r"Thread (\d+): (\d+) bytes", line)
            if match:
                thread_id = int(match.group(1))
                bytes_processed = int(match.group(2))
                data[thread_id] = bytes_processed
    return data

def plot_data(data):
    if not data:
        print("No data to plot.")
        return
        
    threads = list(data.keys())
    bytes_p = list(data.values())
    
    plt.figure(figsize=(10, 6))
    plt.bar(threads, bytes_p)
    plt.xlabel('Thread ID')
    plt.ylabel('Bytes Processed')
    plt.title('Thread Workload Imbalance (8 Threads)')
    plt.savefig('plots/thread_imbalance.png')
    print("Plot saved to plots/thread_imbalance.png")

if __name__ == "__main__":
    if not os.path.exists("results"): os.makedirs("results")
    if not os.path.exists("plots"): os.makedirs("plots")
    
    run_benchmark()
    data = parse_logs()
    plot_data(data)
