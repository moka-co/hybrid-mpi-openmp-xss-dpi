#ifndef PERFORMANCE_H
#define PERFORMANCE_H

#include "config.h"

/**
 * Schema for capturing performance metrics during execution.
 */
typedef struct {
    double exec_time;        // Total execution time in seconds
    double throughput_mb_s;  // Throughput in MB/s
    double speedup;          // Speedup compared to sequential baseline
    double efficiency;       // Parallel efficiency
} PerformanceMetrics;

/**
 * Calculates performance metrics.
 */
void compute_metrics(PerformanceMetrics *metrics, double exec_time, long bytes_scanned, double baseline_time, int num_workers);

/**
 * Prints a performance report for a specific strategy.
 */
void print_performance_report(const Config *cfg, const char *strategy_name, const PerformanceMetrics *metrics, int num_workers);

/**
 * Prints a comparison report comparing multiple strategies.
 */
void print_comparison(const PerformanceMetrics *m_seq, const PerformanceMetrics *m_omp, const PerformanceMetrics *m_mpi, const PerformanceMetrics *m_hybrid);

/**
 * Exports collected performance metrics to a CSV file.
 */
void export_metrics_to_csv(const Config *cfg, int num_ranks, const PerformanceMetrics *metrics_seq, const PerformanceMetrics *metrics_a, const PerformanceMetrics *metrics_b);

#endif // PERFORMANCE_H
