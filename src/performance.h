#ifndef PERFORMANCE_H
#define PERFORMANCE_H

#include "config.h"

// Schema for performance metrics
typedef struct {
    double exec_time;
    double throughput_mb_s;
    double speedup;
    double efficiency;
} PerformanceMetrics;

// Function declarations for performance metrics and reporting
void compute_metrics(PerformanceMetrics *metrics, double exec_time, long bytes_scanned, double baseline_time, int num_workers);
void print_performance_report(const Config *cfg, const char *strategy_name, const PerformanceMetrics *metrics, int num_workers);
void print_comparison(const PerformanceMetrics *m_seq, const PerformanceMetrics *m_omp, const PerformanceMetrics *m_mpi, const PerformanceMetrics *m_hybrid);
void export_metrics_to_csv(const Config *cfg, int num_ranks, const PerformanceMetrics *metrics_seq, const PerformanceMetrics *metrics_a, const PerformanceMetrics *metrics_b);

#endif // PERFORMANCE_H
