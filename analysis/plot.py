import json
import os
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import re

def plot_packet_size_distribution(sizes, output_dir):
    plt.figure(figsize=(10, 6))
    sns.histplot(sizes, bins=50, kde=True)
    plt.title("Packet Size Distribution")
    plt.xlabel("Packet Size (bytes)")
    plt.ylabel("Frequency")
    plt.grid(True)
    plt.savefig(os.path.join(output_dir, 'packet_size_distribution.png'), dpi=300)
    plt.close()
    print(f"Saved packet_size_distribution.png")

def print_dataset_statistics(output_dir, file_path='../datasets/packets.txt'):
    print(f"\n--- Dataset Statistics: {file_path} ---")
    if not os.path.exists(file_path):
        print("Dataset file not found.")
        return

    sizes = []
    malicious = 0
    total = 0
    
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            parts = line.split('|')
            if len(parts) >= 2:
                try:
                    total += 1
                    if parts[0] == '1':
                        malicious += 1
                    sizes.append(int(parts[1]))
                except ValueError:
                    continue
                    
    if total > 0:
        print(f"Total packets: {total}")
        print(f"Malicious packets: {malicious} ({malicious/total*100:.2f}%)")
        print(f"Benign packets: {total - malicious} ({(total - malicious)/total*100:.2f}%)")
        print(f"Average size: {sum(sizes)/total:.2f} bytes")
        print(f"Max size: {max(sizes)} bytes")
        print(f"Min size: {min(sizes)} bytes")
        plot_packet_size_distribution(sizes, output_dir)
    else:
        print("Dataset is empty.")
    print("-------------------------------------------\n")

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

def plot_speedup(df, output_dir, scheduler):
    scheduler_df = df[df['scheduler'] == scheduler]
    if scheduler_df.empty:
        return
    plt.figure(figsize=(10, 6))
    # Plotting speedup vs threads, colored by processes, faceted by mode
    sns.lineplot(data=scheduler_df, x='threads', y='speedup', hue='processes', style='mode', marker='o')
    plt.title(f"Speedup by Thread Count (MPI vs. MPI+OMP) - {scheduler.capitalize()} Scheduler")
    plt.xlabel("Number of OMP Threads")
    plt.ylabel("Speedup")
    plt.grid(True)
    plt.savefig(os.path.join(output_dir, f'speedup_vs_threads_{scheduler}.png'), dpi=300)
    plt.close()
    print(f"Saved speedup_vs_threads_{scheduler}.png")

def plot_efficiency_vs_threads(df, output_dir, scheduler):
    scheduler_df = df[df['scheduler'] == scheduler]
    if scheduler_df.empty:
        return
    plt.figure(figsize=(10, 6))
    # Plotting efficiency vs threads, colored by processes, faceted by mode
    sns.lineplot(data=scheduler_df, x='threads', y='efficiency', hue='processes', style='mode', marker='o')
    plt.title(f"Efficiency by Thread Count (MPI vs. MPI+OMP) - {scheduler.capitalize()} Scheduler")
    plt.xlabel("Number of OMP Threads")
    plt.ylabel("Efficiency")
    plt.grid(True)
    plt.savefig(os.path.join(output_dir, f'efficiency_vs_threads_{scheduler}.png'), dpi=300)
    plt.close()
    print(f"Saved efficiency_vs_threads_{scheduler}.png")

def plot_heatmaps(df, output_dir, scheduler):
    scheduler_df = df[df['scheduler'] == scheduler]
    if scheduler_df.empty:
        return
    # Throughput heatmap
    pivot_throughput = scheduler_df.pivot_table(index='processes', columns='threads', values='throughput_mb_s', aggfunc='mean')
    
    plt.figure(figsize=(8, 6))
    sns.heatmap(pivot_throughput, annot=True, cmap='viridis', fmt='.1f')
    plt.title(f"Throughput Heatmap (MB/s) - {scheduler.capitalize()} Scheduler")
    plt.savefig(os.path.join(output_dir, f'throughput_heatmap_{scheduler}.png'), dpi=300)
    plt.close()
    print(f"Saved throughput_heatmap_{scheduler}.png")
    
    # Speedup heatmap
    plt.figure(figsize=(10, 6))
    sns.heatmap(scheduler_df.pivot_table(index='processes', columns='threads', values='speedup'), annot=True, cmap='coolwarm', fmt='.2f')
    plt.title(f"Speedup Heatmap - {scheduler.capitalize()} Scheduler")
    plt.savefig(os.path.join(output_dir, f'speedup_heatmap_{scheduler}.png'), dpi=300)
    plt.close()
    
def plot_comparison(df, output_dir, metric='speedup'):
    # Filter for threads > 1
    df_filtered = df[df['threads'] > 1].copy()
    if df_filtered.empty or len(df_filtered['scheduler'].unique()) < 2:
        print(f"Not enough data for comparison plot: {metric}. Need multiple schedulers and threads > 1.")
        return

    # Combine processes and threads for x-axis
    df_filtered['config'] = df_filtered.apply(lambda x: f"P{x['processes']}T{x['threads']}", axis=1)

    plt.figure(figsize=(12, 6))
    # Hue by scheduler
    sns.barplot(data=df_filtered, x='config', y=metric, hue='scheduler')
    plt.title(f"Comparison of {metric.capitalize()} (Static vs Dynamic Scheduler)")
    plt.xlabel("Configuration (Processes x Threads)")
    plt.ylabel(metric.capitalize())
    plt.xticks(rotation=45)
    plt.grid(True)
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, f'comparison_{metric}.png'), dpi=300)
    plt.close()
    print(f"Saved comparison_{metric}.png")

if __name__ == "__main__":
    results_dir = 'results'
    output_dir = 'plots'
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
    print_dataset_statistics(output_dir)
    
    df = load_data(results_dir)
    if not df.empty:
        print("Data loaded successfully.")
        df = calculate_metrics(df)
        
        for scheduler in df['scheduler'].unique():
            print(f"Generating plots for scheduler: {scheduler}")
            plot_speedup(df, output_dir, scheduler)
            plot_efficiency_vs_threads(df, output_dir, scheduler)
            plot_heatmaps(df, output_dir, scheduler)
        
        # New comparison plots
        plot_comparison(df, output_dir, metric='speedup')
        plot_comparison(df, output_dir, metric='efficiency')
        
        print("Plots generated.")
    else:
        print("No data loaded. Check the results directory.")
