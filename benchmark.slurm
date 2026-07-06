#!/bin/bash
#SBATCH --job-name=dpi_bench
#SBATCH --partition=compute-fleet
#SBATCH --exclusive
#SBATCH --output=/shared/results/%x_n%N_p%j.out

# These are passed in from the driver script as env vars via sbatch --export
#   NP        = number of MPI ranks (== --ntasks / nodes, 1 per node)
#   THREADS   = OMP threads per rank (== --cpus-per-task)
#   STRATEGY  = static | dynamic
#   REP       = repetition index (for filenames/logging only)

cd /shared/

export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK
export OMP_SCHEDULE=$STRATEGY

echo "=== Run: nodes=$SLURM_JOB_NUM_NODES np=$NP threads=$OMP_NUM_THREADS strategy=$STRATEGY rep=$REP ==="

mpirun -np $NP --map-by node -wdir /shared/hybrid-mpi-openmp-xss-dpi \
  ./tests/benchmarks/benchmark_ac_pt \
  --strategy $STRATEGY \
  --omp-threads $OMP_NUM_THREADS \
  --schedule $STRATEGY,16 \
  --pattern-file datasets/patterns.txt \
  --dataset-file datasets/packets.txt \
  --tag "n${SLURM_JOB_NUM_NODES}_p${NP}_t${OMP_NUM_THREADS}_${STRATEGY}_rep${REP}"