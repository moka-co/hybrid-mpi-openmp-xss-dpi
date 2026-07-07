import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import os

def plot_packet_size_comparison(results_file, output_dir):
    if not os.path.exists(results_file):
        print(f"Results file not found: {results_file}")
        return
    
    # Load the data
    df = pd.read_csv(results_file)
    
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    # Plotting
    plt.figure(figsize=(10, 6))
    
    # Use a log scale for x-axis as packet sizes often increase exponentially
    sns.lineplot(data=df, x='Packets', y='Throughput (MB/s)', marker='o')
    plt.xscale('log')
    plt.title("Throughput vs. Number of Packets")
    plt.xlabel("Number of Packets (log scale)")
    plt.ylabel("Throughput (MB/s)")
    plt.grid(True, which="both", ls="-", alpha=0.5)
    
    output_path = os.path.join(output_dir, 'throughput_vs_packets.png')
    plt.savefig(output_path, dpi=300)
    plt.close()
    
    print(f"Plot saved to {output_path}")

if __name__ == "__main__":
    results_file = "results/benchmark_comparison.csv"
    output_dir = "plots"
    
    plot_packet_size_comparison(results_file, output_dir)
