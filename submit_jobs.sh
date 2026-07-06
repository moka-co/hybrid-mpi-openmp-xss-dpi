#!/bin/bash
# Submits the full test matrix: 11 configs x 2 schedulers x N reps

dos2unix benchmark.slurm

REPS=10
SCHEDULERS=("static" "dynamic")

# Format: nodes ntasks_per_node cpus_per_task total_cores total_computational_cores
declare -a CONFIGS=(
  "1 1 1 1" 
  "1 1 2 2"
  "1 1 4 4"
  "1 1 8 8"
  "2 1 4 8"
  "2 1 8 16"
  "4 1 4 16"
  "4 1 8 32"
  "8 1 2 16"
  "8 1 4 32"
  "8 1 8 64"
)

for CONFIG in "${CONFIGS[@]}"; do
  read -r NODES NTASKS_PER_NODE CPUS_PER_TASK TOTAL_CORES <<< "$CONFIG"
  NP=$NODES   # one MPI rank per node

  for STRATEGY in "${SCHEDULERS[@]}"; do
    for REP in $(seq 1 $REPS); do
      sbatch \
        --nodes=$NODES \
        --ntasks=$NODES \
        --ntasks-per-node=$NTASKS_PER_NODE \
        --cpus-per-task=$CPUS_PER_TASK \
        --export=ALL,NP=$NP,STRATEGY=$STRATEGY,REP=$REP \
        benchmark.slurm
      sleep 1   # small stagger to avoid hammering the scheduler
    done
  done
done