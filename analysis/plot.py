import json
import os
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import re

def load_data(results_dir):
    data = []
    # Pattern to extract p and t from filenames like benchmark_ac_p2.json or benchmark_ac_pt2_4.json
    # benchmark_ac_p<processes>.json
    # benchmark_ac_pt<processes>_<threads>.json
    for filename in os.listdir(results_dir):
        if filename.endswith('.json'):
            file_path = os.path.join(results_dir, filename)
            try:
                with open(file_path, 'r') as f:
                    record = json.load(f)
                    
                    # Extract values from filename if necessary, or rely on JSON
                    processes = record['Configuration']['processes']
                    threads = record['Configuration']['threads']
                    
                    flat_record = {
                        'processes': processes,
                        'threads': threads,
                        'throughput_mb_s': record['Results']['throughput_mb_s'],
                        'mode': 'MPI' if '_pt' not in filename else 'MPI+OMP'
                    }
                    data.append(flat_record)
            except (json.JSONDecodeError, KeyError) as e:
                print(f"Skipping {filename}: {e}")
    return pd.DataFrame(data)

def plot_speedup(df, output_dir):
    # Find baseline: (processes=1, threads=1)
    baseline = df[(df['processes'] == 1) & (df['threads'] == 1)]
    if baseline.empty:
        # Fallback if no 1x1 benchmark, take lowest throughput
        baseline_throughput = df['throughput_mb_s'].min()
        print(f"Using minimum throughput {baseline_throughput} as baseline.")
    else:
        baseline_throughput = baseline['throughput_mb_s'].iloc[0]
        
    df['speedup'] = df['throughput_mb_s'] / baseline_throughput

    plt.figure(figsize=(10, 6))
    # Plotting speedup vs threads, colored by processes, faceted by mode
    sns.lineplot(data=df, x='threads', y='speedup', hue='processes', style='mode', marker='o')
    plt.title("Speedup by Thread Count (MPI vs. MPI+OMP)")
    plt.xlabel("Number of OMP Threads")
    plt.ylabel("Speedup")
    plt.grid(True)
    plt.savefig(os.path.join(output_dir, 'speedup_vs_threads.png'), dpi=300)
    plt.close()
    print("Saved speedup_vs_threads.png")

def plot_efficiency(df, output_dir):
    # Efficiency heatmap
    pivot = df.pivot_table(index='processes', columns='threads', values='throughput_mb_s', aggfunc='mean')
    
    plt.figure(figsize=(8, 6))
    sns.heatmap(pivot, annot=True, cmap='viridis', fmt='.1f')
    plt.title("Throughput Heatmap (MB/s)")
    plt.savefig(os.path.join(output_dir, 'throughput_heatmap.png'), dpi=300)
    plt.close()
    print("Saved throughput_heatmap.png")

if __name__ == "__main__":
    results_dir = '../results'
    output_dir = 'plots'
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    df = load_data(results_dir)
    if not df.empty:
        print("Data loaded successfully.")
        print(df)
        plot_speedup(df, output_dir)
        plot_efficiency(df, output_dir)
        print("Plots generated.")
    else:
        print("No data loaded. Check the results directory.")
