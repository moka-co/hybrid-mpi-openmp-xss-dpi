import re
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import os

def parse_log(log_path):
    data = {}
    with open(log_path, "r") as f:
        for line in f:
            match = re.search(r"Thread (\d+): (\d+) bytes", line)
            if match:
                data[int(match.group(1))] = int(match.group(2))
    return data

def plot_comparison(results, output_dir):
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
    plt.title("Thread Workload by OpenMP Schedule")
    plt.legend(title="OMP_SCHEDULE")
    
    # Force full scalar notation instead of scientific notation
    plt.gca().yaxis.set_major_formatter(ticker.ScalarFormatter(useOffset=False))
    plt.ticklabel_format(style='plain', axis='y')
    
    plt.tight_layout()

    output_path = os.path.join(output_dir, "thread_imbalance_comparison.png")
    plt.savefig(output_path, dpi=300)
    print(f"Plot saved to {output_path}")

if __name__ == "__main__":
    results_dir = 'results'
    output_dir = 'plots'
    os.makedirs(output_dir, exist_ok=True)
    
    results = {
        'static': parse_log(os.path.join(results_dir, 'thread_imbalance_static.log')),
        'dynamic': parse_log(os.path.join(results_dir, 'thread_imbalance_dynamic.log'))
    }
    
    plot_comparison(results, output_dir)
