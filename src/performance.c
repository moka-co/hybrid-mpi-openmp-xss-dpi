#include "performance.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * Computes performance metrics based on execution time and data processed.
 */
void compute_metrics(PerformanceMetrics *metrics, double exec_time, long bytes_scanned, double baseline_time, int num_workers) {
    metrics->exec_time = exec_time;
    metrics->throughput_mb_s = ((double)bytes_scanned / 1e6) / exec_time;
    metrics->speedup = baseline_time / exec_time;
    metrics->efficiency = metrics->speedup / (double)num_workers;
}

/**
 * Prints a performance report for a specific strategy.
 */
void print_performance_report(const Config *cfg, const char *strategy_name, const PerformanceMetrics *metrics, int num_workers) {
    printf("\n=== Performance Report (%s) ===\n", strategy_name);
    printf("  Loaded Signatures Count:%u\n", cfg->num_patterns);
    printf("  Number of Workers:      %d\n", num_workers);
    printf("  Execution Time:         %f s\n", metrics->exec_time);
    printf("  Throughput:             %f MB/s\n", metrics->throughput_mb_s);
    printf("  Calculated Speedup:     %fx\n", metrics->speedup);
    printf("  Parallel Efficiency:    %f\n", metrics->efficiency);
    printf("======================================\n");
}

/**
 * Prints a comparison report across different strategies.
 */
void print_comparison(const PerformanceMetrics *m_seq, const PerformanceMetrics *m_omp, const PerformanceMetrics *m_mpi, const PerformanceMetrics *m_hybrid) {
    printf("\n=== Performance Comparison Report ===\n");
    printf("%-12s | %-12s | %-12s\n", "Strategy", "Exec Time (s)", "Speedup");
    printf("-------------|--------------|-------------\n");
    if (m_seq) printf("%-12s | %-12.4f | %-12.4f\n", "Sequential", m_seq->exec_time, m_seq->speedup);
    if (m_omp) printf("%-12s | %-12.4f | %-12.4f\n", "OMP", m_omp->exec_time, m_omp->speedup);
    if (m_mpi) printf("%-12s | %-12.4f | %-12.4f\n", "MPI", m_mpi->exec_time, m_mpi->speedup);
    if (m_hybrid) printf("%-12s | %-12.4f | %-12.4f\n", "Hybrid", m_hybrid->exec_time, m_hybrid->speedup);
    printf("======================================\n");
}

/**
 * Exports metrics to a CSV file.
 */
void export_metrics_to_csv(const Config *cfg, int num_ranks, const PerformanceMetrics *metrics_seq, const PerformanceMetrics *metrics_a, const PerformanceMetrics *metrics_b) {
    if (cfg->output_file[0] == '\0' || strcmp(cfg->output_format, "csv") != 0) return;

    int file_exists = (access(cfg->output_file, F_OK) == 0);
    FILE *csv = fopen(cfg->output_file, "a");
    
    if (csv) {
        if (!file_exists) {
            fprintf(csv, "mpi_ranks,omp_threads,b_schedule_type,b_schedule_chunk,global_packets,"
                         "seq_time,seq_throughput_mbs,seq_speedup,seq_efficiency,"
                         "a_time,a_throughput_mbs,a_speedup,a_efficiency,"
                         "b_time,b_throughput_mbs,b_speedup,b_efficiency\n");
        }
        fprintf(csv, "%d,%u,%s,%u,%u,"
                     "%f,%f,%f,%f,"
                     "%f,%f,%f,%f,"
                     "%f,%f,%f,%f\n",
                num_ranks, cfg->num_omp_threads, cfg->schedule_type, cfg->schedule_chunk, cfg->packet_count,
                metrics_seq->exec_time, metrics_seq->throughput_mb_s, metrics_seq->speedup, metrics_seq->efficiency,
                metrics_a->exec_time, metrics_a->throughput_mb_s, metrics_a->speedup, metrics_a->efficiency,
                metrics_b->exec_time, metrics_b->throughput_mb_s, metrics_b->speedup, metrics_b->efficiency);
        fclose(csv);
        printf("Metrics appended to CSV logging sink: %s\n", cfg->output_file);
    } else {
        fprintf(stderr, "Error: Failed to write metrics to CSV destination path: %s\n", cfg->output_file);
    }
}
