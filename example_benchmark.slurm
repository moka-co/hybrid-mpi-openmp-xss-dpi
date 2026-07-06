#!/bin/bash
#SBATCH --job-name=benchmark_p
#SBATCH --partition=compute-fleet
#SBATCH --nodes=4
#SBATCH --ntasks=4
#SBATCH --ntasks-per-node=1
#SBATCH --output=/shared/benchmark_p_%j.out

cd /shared/

mpirun -np 4 --map-by node -wdir /shared/ ./tests/benchmarks/benchmark_ac_p