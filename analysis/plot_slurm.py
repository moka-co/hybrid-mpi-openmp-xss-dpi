import json
import os
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import re

# Reuse the plotting functions from analysis/plot.py
def plot_speedup(df, output_dir, scheduler):
    scheduler_df = df[df['scheduler'] == scheduler]
    if scheduler_df.empty:
        return
    plt.figure(figsize=(10, 6))
    # Plotting speedup vs threads, colored by processes
    sns.lineplot(data=scheduler_df, x='threads', y='speedup', hue='processes', style='mode', marker='o')
    plt.title(f"Speedup by Thread Count - {scheduler.capitalize()} Scheduler\nAWS ParallelCluster cin6.2xlarge")
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
    sns.lineplot(data=scheduler_df, x='threads', y='efficiency', hue='processes', style='mode', marker='o')
    plt.title(f"Efficiency by Thread Count - {scheduler.capitalize()} Scheduler\nAWS ParallelCluster cin6.2xlarge")
    plt.xlabel("Number of OMP Threads")
    plt.ylabel("Efficiency")
    plt.grid(True)
    plt.savefig(os.path.join(output_dir, f'efficiency_vs_threads_{scheduler}.png'), dpi=300)
    plt.close()
    print(f"Saved efficiency_vs_threads_{scheduler}.png")

def plot_heatmap(df, output_dir, scheduler, metric, title):
    scheduler_df = df[df['scheduler'] == scheduler]
    if scheduler_df.empty:
        return
    
    pivot_data = scheduler_df.pivot_table(index='processes', columns='threads', values=metric, aggfunc='mean')
    
    if pivot_data.empty:
        return

    plt.figure(figsize=(8, 6))
    sns.heatmap(pivot_data, annot=True, cmap='viridis', fmt='.2f')
    plt.title(f"{title} - {scheduler.capitalize()} Scheduler\nAWS ParallelCluster cin6.2xlarge")
    plt.savefig(os.path.join(output_dir, f'{metric}_heatmap_{scheduler}.png'), dpi=300)
    plt.close()
    print(f"Saved {metric}_heatmap_{scheduler}.png")

def plot_heatmaps(df, output_dir, scheduler):
    plot_heatmap(df, output_dir, scheduler, 'throughput_mb_s', 'Throughput Heatmap (MB/s)')
    plot_heatmap(df, output_dir, scheduler, 'speedup', 'Speedup Heatmap')
    plot_heatmap(df, output_dir, scheduler, 'efficiency', 'Efficiency Heatmap')

def plot_comparison(df, output_dir, metric='speedup'):
    df_filtered = df[df['threads'] > 1].copy()
    if df_filtered.empty or len(df_filtered['scheduler'].unique()) < 2:
        return

    df_filtered['config'] = df_filtered.apply(lambda x: f"P{x['processes']}T{x['threads']}", axis=1)

    plt.figure(figsize=(12, 6))
    sns.barplot(data=df_filtered, x='config', y=metric, hue='scheduler')
    plt.title(f"Comparison of {metric.capitalize()} (Static vs Dynamic Scheduler)\nAWS ParallelCluster cin6.2xlarge")
    plt.xlabel("Configuration (Processes x Threads)")
    plt.ylabel(metric.capitalize())
    plt.xticks(rotation=45)
    plt.grid(True)
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, f'comparison_{metric}.png'), dpi=300)
    plt.close()
    print(f"Saved comparison_{metric}.png")

def parse_slurm_output(file_path):
    records = []
    with open(file_path, 'r') as f:
        content = f.read()
    
    # Split by run
    runs = re.split(r'=== Run:', content)
    for run in runs:
        if not run.strip():
            continue
        
        # Extract config from header
        header_match = re.search(r'nodes=(\d+) np=(\d+) threads=(\d+) strategy=(\w+) rep=(\d+)', run)
        if not header_match:
            continue
            
        nodes, np, threads, strategy, rep = header_match.groups()
        
        # Extract JSON
        json_match = re.search(r'(\{.*\})', run, re.DOTALL)
        if json_match:
            try:
                data = json.loads(json_match.group(1))
                record = {
                    'nodes': int(nodes),
                    'processes': int(np),
                    'threads': int(threads),
                    'scheduler': strategy,
                    'rep': int(rep),
                    'throughput_mb_s': data['Results']['throughput_mb_s'],
                    'scan_time_sec': data['Results']['scan_time_sec'],
                    'mode': 'MPI' if int(threads) == 1 else 'MPI+OMP'
                }
                records.append(record)
            except (KeyError, json.JSONDecodeError):
                continue
    return records

def load_data(results_dir):
    data = []
    for root, dirs, files in os.walk(results_dir):
        for filename in files:
            if filename.endswith('.out'):
                file_path = os.path.join(root, filename)
                data.extend(parse_slurm_output(file_path))
    
    df = pd.DataFrame(data)
    # Aggregate repetitions
    if not df.empty:
        df = df.groupby(['nodes', 'processes', 'threads', 'scheduler', 'mode']).mean(numeric_only=True).reset_index()
    return df

def calculate_metrics(df):
    baseline = df[(df['processes'] == 1) & (df['threads'] == 1)]
    if baseline.empty:
        baseline_throughput = df['throughput_mb_s'].min()
    else:
        baseline_throughput = baseline['throughput_mb_s'].iloc[0]
        
    df['speedup'] = df['throughput_mb_s'] / baseline_throughput
    df['efficiency'] = df['speedup'] / (df['processes'] * df['threads'])
    return df

if __name__ == "__main__":
    results_dir = './results-aws/results'
    output_dir = 'plots/plots_slurm'
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    df = load_data(results_dir)
    if not df.empty:
        print(f"Loaded {len(df)} configurations.")
        df = calculate_metrics(df)
        
        for scheduler in df['scheduler'].unique():
            plot_speedup(df, output_dir, scheduler)
            plot_efficiency_vs_threads(df, output_dir, scheduler)
            plot_heatmaps(df, output_dir, scheduler)
        
        plot_comparison(df, output_dir, metric='speedup')
        plot_comparison(df, output_dir, metric='efficiency')
    else:
        print("No data loaded. Check the results-aws/results directory.")
