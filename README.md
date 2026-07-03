# hybrid-mpi-openmp-xss-dpi
An hybrid Open MPI + OpenMP Deep Packet Inspection for XSS detection


## Getting started

```bash
make test # to run a basic test
```

It is highly suggested to run the benchmarks in the Makefile:
```bash
make benchmark
make benchmark_t NUM_THREADS=4
make benchmark_p NUM_PROCESS=2
make benchmark_pt NUM_PROCESS=2 NUM_THREADS=4
```

Alternatively, you can run all experiments automatically:
```bash
python3 run_experiments.py
```

Results are saved under `results/`. After running these, it is possible to generate plots:
```
cd analysis
python3 -m uv run ./plot.py 
```

The output will be saved under `analysis/plots/`.

