// tests/benchmarks/benchmark_ac.c
//
// File: benchmark_ac.c
// Description: Performance baseline for Aho-Corasick pattern matching
//              in a single-threaded execution context.
//
// Compile: gcc -O3 -Wall -Isrc/ -o tests/benchmarks/benchmark_ac tests/benchmarks/benchmark_ac.c src/pattern_matching.c
// Run: ./tests/benchmarks/benchmark_ac

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "../../src/dataset.h"
#include "../../src/pattern_matching.h"
#include "../../src/config.h"

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
 * Main execution: Runs single-threaded Aho-Corasick benchmark.
 */
int main(int argc, char *argv[])
{
    // Initialize configuration
    Config cfg;
    init_default_config(&cfg);
    parse_arguments(argc, argv, &cfg);

    printf("Pattern Matching Single-Threaded Performance Baseline\n\n");
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
    printf("  Average packet size: %.1f bytes\n", (double)total_bytes / cfg.packet_count);
    printf("  Min/Max packet size (first/last): %zu / %zu bytes\n\n",
           packets[0].len, packets[cfg.packet_count - 1].len);

    // Scan all packets and time it
    printf("Scanning packets (measuring time)...\n");

    double scan_start = get_time_sec();
    uint64_t total_matches = 0;

    for (int i = 0; i < cfg.packet_count; i++) {
        ACMatchList ml = ac_scan(ac, packets[i].data, packets[i].len);
        total_matches += ml.count;
        ac_free_matches(&ml);
    }

    double scan_end = get_time_sec();
    double scan_time = scan_end - scan_start;

    printf("  Scan time: %.6f sec\n", scan_time);
    printf("  Total matches found: %lu\n\n", total_matches);

    // Compute metrics
    double throughput_mb_per_sec = (double)total_bytes / 1e6 / scan_time;
    double packets_per_sec = (double)cfg.packet_count / scan_time;
    double avg_time_per_packet_us = (scan_time / cfg.packet_count) * 1e6;
    double avg_time_per_byte_ns = (scan_time / total_bytes) * 1e9;

    // Generate JSON
    char json_buffer[4096];
    snprintf(json_buffer, sizeof(json_buffer),
        "{\n"
        "  \"Configuration\": {\n"
        "    \"patterns_count\": %d,\n"
        "    \"automaton_states\": %d,\n"
        "    \"test_packets\": %d,\n"
        "    \"total_data_scanned_mb\": %.2f,\n"
        "    \"avg_packet_size\": %.1f,\n"
        "    \"processes\": 1,\n"
        "    \"threads\": 1,\n"
        "    \"scheduler\": \"static\"\n"
        "  },\n"
        "  \"Results\": {\n"
        "    \"scan_time_sec\": %.6f,\n"
        "    \"total_matches\": %lu,\n"
        "    \"throughput_mb_s\": %.2f,\n"
        "    \"packets_per_sec\": %.0f,\n"
        "    \"avg_time_per_packet_us\": %.3f,\n"
        "    \"avg_time_per_byte_ns\": %.2f\n"
        "  },\n"
        "  \"BottleneckNotes\": {\n"
        "    \"automaton_states\": %d,\n"
        "    \"bytes_scanned_per_state\": %.0f,\n"
        "    \"ac_state_struct_size_bytes\": %zu,\n"
        "    \"goto_table_size_bytes\": %zu\n"
        "  }\n"
        "}",
        num_patterns, ac->num_states, cfg.packet_count, (double)total_bytes / 1e6, (double)total_bytes / cfg.packet_count,
        scan_time, total_matches, throughput_mb_per_sec, packets_per_sec, avg_time_per_packet_us, avg_time_per_byte_ns,
        ac->num_states, (double)total_bytes / ac->num_states, sizeof(ACState), AC_ALPHABET_SIZE * sizeof(int));

    // Print to stdout
    printf("%s\n", json_buffer);

    // Save to file
    FILE *f = fopen(cfg.output_file, "w");
    if (f) {
        fprintf(f, "%s\n", json_buffer);
        fclose(f);
    }

    // Cleanup
    free_packets(packets, cfg.packet_count);

    free_patterns_list(patterns, num_patterns);

    ac_free(ac);

    printf("=== Benchmark Complete ===\n");
    return 0;
}