#!/usr/bin/env python3
import subprocess
import os
import sys

# Experiment configurations
SCHEDULERS = ["static", "dynamic,16"]
THREADS = [2, 4, 8]
PROCESSES = [1, 2, 4]
HYBRID_CONFIGS = [
    (1, 2), (1, 4), (1, 8),
    (2, 2), (2, 4), (2, 8),
    (4, 2), (4, 4), (4, 8)
]

def run_cmd(cmd, env=None):
    print(f"Running: {' '.join(cmd)}")
    subprocess.run(cmd, env=env, check=True)

def main():
    # Set the working directory to the current shell location
    os.chdir(os.getcwd())

    # Ensure project is compiled
    run_cmd(["make"])

    if not os.path.exists("results"):
        os.makedirs("results")

    # Base environment
    base_env = os.environ.copy()

    for sched in SCHEDULERS:
        print(f"--- Running experiments with scheduler: {sched} ---")
        current_env = base_env.copy()
        current_env["OMP_SCHEDULE"] = sched

        # 1. Sequential (doesn't depend on OMP_SCHEDULE)
        if sched == "static":
            run_cmd(["./tests/benchmarks/benchmark_ac"])

        # 2. Multithread
        for t in THREADS:
            env = current_env.copy()
            env["OMP_NUM_THREADS"] = str(t)
            run_cmd(["./tests/benchmarks/benchmark_ac_t"], env=env)

        # 3. Multiprocess (doesn't depend on OMP_SCHEDULE directly as it's MPI)
        if sched == "static":
            for p in PROCESSES:
                run_cmd(["mpirun", "--allow-run-as-root", "-np", str(p), "./tests/benchmarks/benchmark_ac_p"])

        # 4. Multiprocess + Multithread
        for p, t in HYBRID_CONFIGS:
            env = current_env.copy()
            env['OMP_NUM_THREADS'] = str(t)
            run_cmd(["mpirun", "--allow-run-as-root", "-np", str(p), "-x", "OMP_NUM_THREADS", "-x", "OMP_SCHEDULE", "./tests/benchmarks/benchmark_ac_pt"], env=env)

if __name__ == "__main__":
    main()
