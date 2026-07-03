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
                        'scheduler': record['Configuration'].get('scheduler', 'static'),
                        'throughput_mb_s': record['Results']['throughput_mb_s'],
                        'scan_time_sec': record['Results']['scan_time_sec'],
                        'mode': 'MPI' if '_pt' not in filename else 'MPI+OMP'
                    }
                    data.append(flat_record)
            except (json.JSONDecodeError, KeyError) as e:
                print(f"Skipping {filename}: {e}")
    return pd.DataFrame(data)

def calculate_metrics(df):
    # Find baseline: (processes=1, threads=1)
    baseline = df[(df['processes'] == 1) & (df['threads'] == 1)]
    if baseline.empty:
        # Fallback if no 1x1 benchmark, take lowest throughput for speedup baseline
        baseline_throughput = df['throughput_mb_s'].min()
        baseline_time = df['scan_time_sec'].max()
        print(f"Using min throughput {baseline_throughput} and max scan time {baseline_time} as baseline.")
    else:
        baseline_throughput = baseline['throughput_mb_s'].iloc[0]
        baseline_time = baseline.iloc[0]['scan_time_sec']
        
    df['speedup'] = df['throughput_mb_s'] / baseline_throughput
    # Efficiency is speedup / total cores (processes * threads)
    df['efficiency'] = df['speedup'] / (df['processes'] * df['threads'])
    return df

def plot_speedup(df, output_dir):
    plt.figure(figsize=(10, 6))
    # Plotting speedup vs threads, colored by processes, faceted by mode
    sns.lineplot(data=df, x='threads', y='speedup', hue='processes', style='mode', marker='o')
    plt.title("Speedup by Thread Count (MPI vs. MPI+OMP) - Static Scheduler")
    plt.xlabel("Number of OMP Threads")
    plt.ylabel("Speedup")
    plt.grid(True)
    plt.savefig(os.path.join(output_dir, 'speedup_vs_threads.png'), dpi=300)
    plt.close()
    print("Saved speedup_vs_threads.png")

def plot_efficiency_vs_threads(df, output_dir):
    plt.figure(figsize=(10, 6))
    # Plotting efficiency vs threads, colored by processes, faceted by mode
    sns.lineplot(data=df, x='threads', y='efficiency', hue='processes', style='mode', marker='o')
    plt.title("Efficiency by Thread Count (MPI vs. MPI+OMP) - Static Scheduler")
    plt.xlabel("Number of OMP Threads")
    plt.ylabel("Efficiency")
    plt.grid(True)
    plt.savefig(os.path.join(output_dir, 'efficiency_vs_threads.png'), dpi=300)
    plt.close()
    print("Saved efficiency_vs_threads.png")

def plot_heatmaps(df, output_dir):
    # Efficiency heatmap
    pivot_throughput = df.pivot_table(index='processes', columns='threads', values='throughput_mb_s', aggfunc='mean')
    
    plt.figure(figsize=(8, 6))
    sns.heatmap(pivot_throughput, annot=True, cmap='viridis', fmt='.1f')
    plt.title("Throughput Heatmap (MB/s) - Static Scheduler")
    plt.savefig(os.path.join(output_dir, 'throughput_heatmap.png'), dpi=300)
    plt.close()
    print("Saved throughput_heatmap.png")
    
    # Speedup heatmap
    plt.figure(figsize=(10, 6))
    sns.heatmap(df.pivot_table(index='processes', columns='threads', values='speedup'), annot=True, cmap='coolwarm', fmt='.2f')
    plt.title("Speedup Heatmap - Static Scheduler")
    plt.savefig(os.path.join(output_dir, 'speedup_heatmap.png'), dpi=300)
    plt.close()
    
    # Efficiency heatmap
    plt.figure(figsize=(10, 6))
    sns.heatmap(df.pivot_table(index='processes', columns='threads', values='efficiency'), annot=True, cmap='coolwarm', fmt='.2f')
    plt.title("Efficiency Heatmap - Static Scheduler")
    plt.savefig(os.path.join(output_dir, 'efficiency_heatmap.png'), dpi=300)
    plt.close()
    print("Saved speedup_heatmap.png and efficiency_heatmap.png")

if __name__ == "__main__":
    results_dir = 'results'
    output_dir = 'plots'
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    df = load_data(results_dir)
    if not df.empty:
        print("Data loaded successfully.")
        df = calculate_metrics(df)
        
        plot_speedup(df, output_dir)
        plot_efficiency_vs_threads(df, output_dir)
        plot_heatmaps(df, output_dir)
        print("Plots generated.")
    else:
        print("No data loaded. Check the results directory.")
