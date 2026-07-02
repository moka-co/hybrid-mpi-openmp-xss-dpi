#!/usr/bin/env python3
import subprocess
import os
import sys

# Experiment configurations
THREADS = [2, 4, 8]
PROCESSES = [1, 2, 4]
HYBRID_CONFIGS = [
    (1, 2), (1, 4), (1, 8),
    (2, 2), (2, 4), (2, 8),
    (4, 2), (4, 4), (4, 8)
]

BASE_SEED = 1024 * 1000

def run_cmd(cmd):
    print(f"Running: {' '.join(cmd)}")
    subprocess.run(cmd, check=True)

def main():
    # Ensure project is compiled
    run_cmd(["make"])

    if not os.path.exists("results"):
        os.makedirs("results")

    # 1. Sequential
    # Using benchmark_ac directly
    run_cmd(["./tests/benchmarks/benchmark_ac"])

    # 2. Multithread
    for t in THREADS:
        # Assuming the benchmark_ac_t binary might need an env var for threads
        # or we just rely on OMP_NUM_THREADS
        env = os.environ.copy()
        env["OMP_NUM_THREADS"] = str(t)
        subprocess.run(["./tests/benchmarks/benchmark_ac_t"], env=env, check=True)

    # 3. Multiprocess
    for p in PROCESSES:
        seed = BASE_SEED + p * 100
        run_cmd(["mpirun", "-np", str(p), "./tests/benchmarks/benchmark_ac_p"])

    # 4. Multiprocess + Multithread
    for p, t in HYBRID_CONFIGS:
        seed = BASE_SEED + p * 100 + t * 10
        env = os.environ.copy()
        env["OMP_NUM_THREADS"] = str(t)
        run_cmd(["mpirun", "-np", str(p), "./tests/benchmarks/benchmark_ac_pt"])

if __name__ == "__main__":
    main()
