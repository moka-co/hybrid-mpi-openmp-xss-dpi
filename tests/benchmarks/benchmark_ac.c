// tests/benchmarks/benchmark_ac.c
// Compile: gcc -O3 -Wall -Isrc/ -o tests/benchmarks/benchmark_ac tests/benchmarks/benchmark_ac.c src/pattern_matching.c
// Run: ./tests/benchmarks/benchmark_ac

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "../../src/pattern_matching.h"

// Simple random number generator (seeded) for reproducibility
static uint32_t rng_state = 42;

static uint32_t rand_u32(void)
{
    rng_state = rng_state * 1103515245 + 12345;
    return (rng_state / 65536) % 32768;
}

// Generate a synthetic packet of random length (between min_len and max_len).
// Fills with pseudo-random bytes.
static uint8_t *gen_random_packet(size_t min_len, size_t max_len, size_t *out_len)
{
    size_t len = min_len + (rand_u32() % (max_len - min_len + 1));
    uint8_t *pkt = (uint8_t *)malloc(len);
    for (size_t i = 0; i < len; i++) {
        pkt[i] = (uint8_t)(rand_u32() & 0xFF);
    }
    *out_len = len;
    return pkt;
}

// Get time in seconds (platform-dependent).
// On Unix: uses clock_gettime.
// On Windows: uses GetTickCount64 (rough approximation).
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

// Load patterns from a file (one pattern per line)
// Grows the array dynamically as patterns are read, no upper bound needed.
static char **load_patterns_from_file(const char *filepath, int *out_count)
{
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        perror("Error opening pattern file");
        return NULL;
    }

    int capacity = 256;
    char **patterns = malloc(capacity * sizeof(char *));
    if (!patterns) {
        fclose(fp);
        return NULL;
    }

    int count = 0;
    char line[4096];

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) == 0) continue;

        if (count >= capacity) {
            capacity *= 2;
            char **grown = realloc(patterns, capacity * sizeof(char *));
            if (!grown) {
                fprintf(stderr, "ERROR: Failed to grow pattern array\n");
                fclose(fp);
                return NULL;
            }
            patterns = grown;
        }

        patterns[count] = strdup(line);
        if (!patterns[count]) {
            fprintf(stderr, "ERROR: Failed to allocate memory for pattern\n");
            fclose(fp);
            return NULL;
        }
        count++;
    }

    fclose(fp);
    *out_count = count;
    return patterns;
}

int main(int argc, char *argv[])
{
    printf("Pattern Matching Single-Threaded Performance Baseline\n\n");

    // Load patterns from file
    printf("Loading XSS patterns from file...\n");
    int num_patterns = 0;
    char **patterns = load_patterns_from_file("datasets-private/string_xss_only.txt",
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
    printf("Generating 1,000,000 synthetic packets...\n");
    int num_packets = 1000000;
    uint8_t **packets = (uint8_t **)malloc(num_packets * sizeof(uint8_t *));
    size_t *packet_lens = (size_t *)malloc(num_packets * sizeof(size_t));
    size_t total_bytes = 0;

    for (int i = 0; i < num_packets; i++) {
        packets[i] = gen_random_packet(64, 8192, &packet_lens[i]);
        total_bytes += packet_lens[i];
    }

    printf("  Packet count: %d\n", num_packets);
    printf("  Total bytes: %.2f MB\n", (double)total_bytes / 1e6);
    printf("  Average packet size: %.1f bytes\n", (double)total_bytes / num_packets);
    printf("  Min/Max packet size: %zu / %zu bytes\n\n",
           packet_lens[0], packet_lens[num_packets - 1]);

    // Scan all packets and time it
    printf("Scanning packets (measuring time)...\n");

    double scan_start = get_time_sec();
    uint64_t total_matches = 0;

    for (int i = 0; i < num_packets; i++) {
        ACMatchList ml = ac_scan(ac, packets[i], packet_lens[i]);
        total_matches += ml.count;
        ac_free_matches(&ml);
    }

    double scan_end = get_time_sec();
    double scan_time = scan_end - scan_start;

    printf("  Scan time: %.6f sec\n", scan_time);
    printf("  Total matches found: %lu\n\n", total_matches);

    // Compute metrics
    double throughput_mb_per_sec = (double)total_bytes / 1e6 / scan_time;
    double packets_per_sec = (double)num_packets / scan_time;
    double avg_time_per_packet_us = (scan_time / num_packets) * 1e6;
    double avg_time_per_byte_ns = (scan_time / total_bytes) * 1e9;

    // Generate JSON
    char json_buffer[4096];
    int len = snprintf(json_buffer, sizeof(json_buffer),
        "{\n"
        "  \"Configuration\": {\n"
        "    \"patterns_count\": %d,\n"
        "    \"automaton_states\": %d,\n"
        "    \"test_packets\": %d,\n"
        "    \"total_data_scanned_mb\": %.2f,\n"
        "    \"avg_packet_size\": %.1f,\n"
        "    \"processes\": 1,\n"
        "    \"threads\": 1\n"
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
        num_patterns, ac->num_states, num_packets, (double)total_bytes / 1e6, (double)total_bytes / num_packets,
        scan_time, total_matches, throughput_mb_per_sec, packets_per_sec, avg_time_per_packet_us, avg_time_per_byte_ns,
        ac->num_states, (double)total_bytes / ac->num_states, sizeof(ACState), AC_ALPHABET_SIZE * sizeof(int));

    // Print to stdout
    printf("%s\n", json_buffer);

    // Save to file
    FILE *f = fopen("results/benchmark_ac.json", "w");
    if (f) {
        fprintf(f, "%s\n", json_buffer);
        fclose(f);
    }

    // Cleanup
    for (int i = 0; i < num_packets; i++)
        free(packets[i]);
    free(packets);
    free(packet_lens);

    for (int i = 0; i < num_patterns; i++)
        free(patterns[i]);
    free(patterns);

    ac_free(ac);

    printf("=== Benchmark Complete ===\n");
    return 0;
}