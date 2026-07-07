import pandas as pd
import matplotlib.pyplot as plt
import os

def plot_benchmark_comparison(csv_path, output_dir):
    if not os.path.exists(csv_path):
        print(f"File not found: {csv_path}")
        return

    df = pd.read_csv(csv_path)
    
    plt.figure(figsize=(10, 6))
    plt.bar(df['Packets'].astype(str), df['Throughput (MB/s)'])
    plt.title("Benchmark Throughput vs. Number of Packets")
    plt.xlabel("Number of Packets")
    plt.ylabel("Throughput (MB/s)")
    plt.grid(axis='y')
    plt.tight_layout()

    output_path = os.path.join(output_dir, "benchmark_comparison.png")
    plt.savefig(output_path, dpi=300)
    print(f"Plot saved to {output_path}")

if __name__ == "__main__":
    csv_path = 'results/benchmark_comparison.csv'
    output_dir = 'plots'
    os.makedirs(output_dir, exist_ok=True)
    
    plot_benchmark_comparison(csv_path, output_dir)
