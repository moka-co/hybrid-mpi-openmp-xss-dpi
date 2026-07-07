import subprocess
import sys
import csv
import re

def run_benchmarks():
    # Configuration
    start_packets = 100000
    max_packets = 100000000  # Safety limit: 100 million packets
    mpi_ranks = 2
    omp_threads = 4
    benchmark_bin = "./tests/benchmarks/benchmark_ac_pt"
    results_file = "results/benchmark_comparison.csv"

    results = []

    packets = start_packets
    while packets <= max_packets:
        print(f"--- Running benchmark with {packets} packets ---")
        
        cmd = [
            "mpirun",
            "-np", str(mpi_ranks),
            "-x", f"OMP_NUM_THREADS={omp_threads}",
            benchmark_bin,
            "--num-packets", str(packets),
            "--omp-threads", str(omp_threads)
        ]
        
        try:
            result = subprocess.run(cmd, check=True, capture_output=True, text=True)
            output = result.stdout
            print(output)
            
            # Extract throughput (assuming "Throughput: X MB/s")
            match = re.search(r"Throughput:\s+([\d\.]+)\s+MB/s", output)
            if match:
                throughput = float(match.group(1))
                results.append((packets, throughput))
            
        except subprocess.CalledProcessError as e:
            print(f"Error running benchmark with {packets} packets: {e}")
            break
        except FileNotFoundError:
            print(f"Benchmark binary not found at {benchmark_bin}.")
            sys.exit(1)
        
        packets *= 10
    
    # Save results to CSV
    with open(results_file, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(["Packets", "Throughput (MB/s)"])
        writer.writerows(results)
    
    print(f"Results saved to {results_file}")

if __name__ == "__main__":
    run_benchmarks()
