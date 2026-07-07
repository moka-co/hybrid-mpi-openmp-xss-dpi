import subprocess
import sys
import os
import csv
import json

def run_benchmarks():
    # Configuration
    start_packets = 10000
    max_packets = 1000000    # Safety limit: 10 million packets (~15-20GB of
                               # packet data at this dataset's avg packet size --
                               # the old 100M cap was ~170GB and would OOM/hang)
    growth_factor = 10
    mpi_ranks = 2
    omp_threads = 4
    benchmark_bin = "./tests/benchmarks/benchmark_ac_pt"
    results_file = "results/benchmark_comparison.csv"

    os.makedirs("results", exist_ok=True)

    if not os.path.exists(benchmark_bin):
        print(f"Benchmark binary not found at {benchmark_bin}.")
        sys.exit(1)

    results = []

    # Write the header up front and append one row per completed run, so a
    # failure partway through the sweep still leaves a usable CSV instead of
    # losing every prior result (the old version only wrote at the very end).
    with open(results_file, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(["Packets", "Throughput (MB/s)"])

    packets = start_packets
    while packets <= max_packets:
        print(f"--- Running benchmark with {packets} packets ---")

        cmd = [
            "mpirun",
            "--allow-run-as-root", "--oversubscribe",  # harmless if not needed;
                                                         # remove if your environment
                                                         # doesn't require them
            "-np", str(mpi_ranks),
            "-x", f"OMP_NUM_THREADS={omp_threads}",
            benchmark_bin,
            "--num-packets", str(packets),
            "--omp-threads", str(omp_threads),
        ]

        try:
            result = subprocess.run(cmd, check=True, capture_output=True, text=True)
            output = result.stdout

            # The output contains JSON, let's find it. It might be surrounded by other logs.
            # Look for the first '{' and last '}'
            start = output.find('{')
            end = output.rfind('}') + 1
            if start != -1 and end != -1:
                json_str = output[start:end]
                try:
                    data = json.loads(json_str)
                    throughput = data['Results']['throughput_mb_s']
                    results.append((packets, throughput))
                    with open(results_file, 'a', newline='') as f:
                        writer = csv.writer(f)
                        writer.writerow([packets, throughput])
                except Exception as e:
                    print(f"Failed to parse JSON output: {e}")
                    print(json_str)
            else:
                print("Failed to find JSON output.")
                print(output)

        except subprocess.CalledProcessError as e:
            # Log the failure and move on to the next packet count instead of
            # aborting the whole sweep -- a single failed run (e.g. a
            # transient MPI/slot issue) used to wipe out every prior result
            # since the CSV was only written after the loop finished.
            print(f"Error running benchmark with {packets} packets: {e}")
            if e.stderr:
                print(e.stderr)
            packets *= growth_factor
            continue
        except FileNotFoundError:
            print(f"Benchmark binary not found at {benchmark_bin}.")
            sys.exit(1)

        packets *= growth_factor

    print(f"Results saved to {results_file}")

if __name__ == "__main__":
    run_benchmarks()