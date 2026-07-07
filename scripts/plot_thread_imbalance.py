import subprocess
import os
import re
import matplotlib.pyplot as plt

BENCHMARK_BIN = "./tests/benchmarks/benchmark_ac_t_logged"
LOG_PATH = "results/thread_imbalance.log"  # fixed path the C binary always writes to
NUM_THREADS = 8
NUM_PACKETS = 1000000
SCHEDULES = ["static", "dynamic"]


def compile_benchmark():
    print("Compiling benchmark...")
    cmd_compile = [
        "gcc", "-O3", "-Wall", "-fopenmp", "-Isrc/",
        "-o", BENCHMARK_BIN,
        "tests/benchmarks/benchmark_ac_t_logged.c",
        "src/pattern_matching.c", "src/dataset.c", "src/config.c",
        "-lm",
    ]
    subprocess.run(cmd_compile, check=True)


def run_benchmark(schedule):
    """Runs the benchmark once with the given OMP_SCHEDULE and returns the
    path to a schedule-tagged copy of the resulting log file.

    The benchmark uses `schedule(runtime)`, so OMP_SCHEDULE is what actually
    picks static vs. dynamic here -- the old script never set it, which
    meant every run silently fell back to the compiler's default (static).
    """
    print(f"Running benchmark with {NUM_THREADS} threads, schedule={schedule}...")

    env = os.environ.copy()
    env["OMP_NUM_THREADS"] = str(NUM_THREADS)
    env["OMP_SCHEDULE"] = schedule

    cmd_run = [
        BENCHMARK_BIN,
        "--num-packets", str(NUM_PACKETS),
        "--omp-threads", str(NUM_THREADS),
    ]
    subprocess.run(cmd_run, env=env, check=True)

    if not os.path.exists(LOG_PATH):
        raise FileNotFoundError(
            f"Expected {LOG_PATH} after running with schedule={schedule}, "
            "but the benchmark didn't produce it."
        )

    # The C binary always writes to the same fixed path, so we rename it to
    # a schedule-specific file immediately -- otherwise the dynamic run
    # would silently overwrite the static run's log.
    tagged_path = f"results/thread_imbalance_{schedule}.log"
    os.replace(LOG_PATH, tagged_path)
    return tagged_path


def parse_log(log_path):
    data = {}
    with open(log_path, "r") as f:
        for line in f:
            match = re.search(r"Thread (\d+): (\d+) bytes", line)
            if match:
                data[int(match.group(1))] = int(match.group(2))
    return data


def summarize(schedule, data):
    if not data:
        print(f"{schedule}: no data parsed")
        return
    values = list(data.values())
    mean = sum(values) / len(values)
    spread_pct = (max(values) - min(values)) / mean * 100
    print(f"{schedule:8s}: {len(values)} threads, "
          f"min={min(values):,} max={max(values):,} "
          f"spread={spread_pct:.2f}% of mean")


def plot_comparison(results):
    """results: dict of schedule_name -> {thread_id: bytes}"""
    if not results or not any(results.values()):
        print("No data to plot.")
        return

    all_thread_ids = sorted({tid for d in results.values() for tid in d})
    schedules = list(results.keys())

    bar_width = 0.8 / len(schedules)
    x = list(range(len(all_thread_ids)))

    plt.figure(figsize=(10, 6))
    for i, schedule in enumerate(schedules):
        values = [results[schedule].get(tid, 0) for tid in all_thread_ids]
        offsets = [xi + i * bar_width for xi in x]
        plt.bar(offsets, values, width=bar_width, label=schedule)

    tick_positions = [xi + bar_width * (len(schedules) - 1) / 2 for xi in x]
    plt.xticks(tick_positions, all_thread_ids)
    plt.xlabel("Thread ID")
    plt.ylabel("Bytes Processed")
    plt.title(f"Thread Workload by OpenMP Schedule "
              f"({NUM_THREADS} threads, {NUM_PACKETS:,} packets)")
    plt.legend(title="OMP_SCHEDULE")
    plt.tight_layout()

    output_path = "plots/thread_imbalance_comparison.png"
    plt.savefig(output_path)
    print(f"Plot saved to {output_path}")


if __name__ == "__main__":
    os.makedirs("results", exist_ok=True)
    os.makedirs("plots", exist_ok=True)

    compile_benchmark()

    results = {}
    for schedule in SCHEDULES:
        log_path = run_benchmark(schedule)
        results[schedule] = parse_log(log_path)

    print()
    for schedule, data in results.items():
        summarize(schedule, data)
    print()

    plot_comparison(results)