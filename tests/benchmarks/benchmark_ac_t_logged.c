// tests/benchmarks/benchmark_ac_t_logged.c
//
// File: benchmark_ac_t_logged.c
// Description: OpenMP-parallelized performance benchmark for Aho-Corasick pattern matching,
//              with logging of bytes processed per thread for load imbalance analysis.
//
// Compile: gcc -O3 -Wall -fopenmp -Isrc/ -o tests/benchmarks/benchmark_ac_t_logged tests/benchmarks/benchmark_ac_t_logged.c src/pattern_matching.c src/dataset.c src/config.c
// Run: OMP_NUM_THREADS=<num_threads> ./tests/benchmarks/benchmark_ac_t_logged

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <omp.h>
#include "../../src/dataset.h"
#include "../../src/pattern_matching.h"
#include "../../src/config.h"

#ifdef _WIN32
#include <windows.h>
#endif

/**
 * Returns the current time in seconds, platform-dependent.
 */
static double get_time_sec(void)
{
#ifdef _WIN32
    return (double)GetTickCount64() / 1000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

/**
 * Main execution: Runs OpenMP-parallelized Aho-Corasick benchmark with per-thread logging.
 */
int main(int argc, char *argv[])
{
    Config cfg;
    // Note: Use command-line arguments to override defaults (e.g., --num-packets <num>)
    int threads_provided = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--omp-threads") == 0 || strcmp(argv[i], "-t") == 0) {
            threads_provided = 1;
            break;
        }
    }
    init_default_config(&cfg);
    parse_arguments(argc, argv, &cfg);
    if (threads_provided) omp_set_num_threads(cfg.num_omp_threads);

    printf("Pattern Matching Multithreaded Performance Baseline (OpenMP, Logged)\n\n");
    print_config(&cfg);

    // Load patterns from file
    printf("Loading XSS patterns from file: %s...\n", cfg.pattern_file);
    int num_patterns = 0;
    char **patterns = load_patterns_from_file(cfg.pattern_file,
                                              &num_patterns);

    if (!patterns || num_patterns == 0) {
        fprintf(stderr, "ERROR: Failed to load patterns\n");
        return 1;
    }

    printf("  Patterns loaded: %d\n\n", num_patterns);

    // Build the automaton
    printf("Building pattern matching automaton...\n");
    double build_start = get_time_sec();
    ACAutomaton *ac = ac_build((const char **)patterns, num_patterns);
    double build_end = get_time_sec();

    if (!ac) {
        fprintf(stderr, "ERROR: Failed to build automaton\n");
        return 1;
    }

    printf("  Automaton states: %d\n", ac->num_states);
    printf("  Build time: %.6f sec\n\n", build_end - build_start);

    // Generate test packets
    printf("Generating %d synthetic packets...\n", cfg.packet_count);
    LengthDistParams lp = { .mu = 7.0, .sigma = 1.0, .min_len = 64, .max_len = 8192 };
    Packet *packets = generate_packets(cfg.packet_count, 42, lp, 0.5, (const char **)patterns, num_patterns);
    if (!packets) {
        fprintf(stderr, "ERROR: Failed to generate packets\n");
        return 1;
    }

    size_t total_bytes = 0;
    for (int i = 0; i < cfg.packet_count; i++) {
        total_bytes += packets[i].len;
    }

    printf("  Packet count: %d\n", cfg.packet_count);
    printf("  Total bytes: %.2f MB\n", (double)total_bytes / 1e6);

    // Scan all packets and time it
    printf("Scanning packets with OpenMP (measuring time)...\n");

    double scan_start = get_time_sec();
    uint64_t total_matches = 0;
    
    // Allocate log array for bytes per thread
    int max_threads = omp_get_max_threads();
    long *bytes_per_thread = (long*)calloc(max_threads, sizeof(long));

    /* Thread-safety assumption: The Aho-Corasick automaton `ac` is read-only.
     * Each OpenMP thread has its own `ACMatchList` to avoid data races. */
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        ACMatchList ml;
        ac_matchlist_init(&ml, 16);

        #pragma omp for schedule(runtime) reduction(+:total_matches)
        for (int i = 0; i < cfg.packet_count; i++) {
            ac_scan_into(ac, packets[i].data, packets[i].len, &ml);
            total_matches += ml.count;
            bytes_per_thread[tid] += packets[i].len;
        }

        ac_free_matches(&ml);
    }

    double scan_end = get_time_sec();
    double scan_time = scan_end - scan_start;

    printf("  Scan time: %.6f sec\n", scan_time);
    printf("  Total matches found: %lu\n\n", total_matches);

    // Log bytes_per_thread
    FILE *log = fopen("results/thread_imbalance.log", "w");
    if (log) {
        for(int t = 0; t < max_threads; t++) {
            fprintf(log, "Thread %d: %ld bytes\n", t, bytes_per_thread[t]);
        }
        fclose(log);
        printf("Thread workload logged to results/thread_imbalance.log\n");
    } else {
        fprintf(stderr, "ERROR: Failed to open results/thread_imbalance.log\n");
    }
    
    free(bytes_per_thread);

    // Cleanup
    free_packets(packets, cfg.packet_count);
    free_patterns_list(patterns, num_patterns);
    ac_free(ac);

    printf("=== Benchmark Complete ===\n");
    return 0;
}
