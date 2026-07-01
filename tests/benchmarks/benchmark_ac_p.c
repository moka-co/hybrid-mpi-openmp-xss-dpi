#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <mpi.h>
#include "../../src/pattern_matching.h"
#include "../../src/dataset.h"

// Load patterns from a file
static char **load_patterns_from_file(const char *filepath, int *out_count)
{
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        perror("Error opening pattern file");
        return NULL;
    }

    int capacity = 256;
    char **patterns = malloc(capacity * sizeof(char *));
    int count = 0;
    char line[4096];

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) == 0) continue;
        if (count >= capacity) {
            capacity *= 2;
            patterns = realloc(patterns, capacity * sizeof(char *));
        }
        patterns[count++] = strdup(line);
    }
    fclose(fp);
    *out_count = count;
    return patterns;
}

int main(int argc, char *argv[])
{
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int num_patterns = 0;
    char **patterns = NULL;
    ACAutomaton *ac = NULL;
    int num_states = 0, capacity = 0;

    // 1. Rank 1 builds automaton and broadcasts it
    if (rank == 1) {
        printf("Rank 1: Building automaton...\n");
        patterns = load_patterns_from_file("datasets-private/string_xss_only.txt", &num_patterns);
        ac = ac_build((const char **)patterns, num_patterns);
        num_states = ac->num_states;
        capacity = ac->capacity;
    }

    MPI_Bcast(&num_patterns, 1, MPI_INT, 1, MPI_COMM_WORLD);
    MPI_Bcast(&num_states, 1, MPI_INT, 1, MPI_COMM_WORLD);
    MPI_Bcast(&capacity, 1, MPI_INT, 1, MPI_COMM_WORLD);

    if (rank != 1) {
        printf("Rank %d: Allocating automaton buffers...\n", rank);
        ac = (ACAutomaton *)calloc(1, sizeof(ACAutomaton));
        ac->num_states = num_states;
        ac->capacity = capacity;
        ac->num_patterns = num_patterns;
        ac->states = (ACState *)malloc(sizeof(ACState) * capacity);
        ac->output_next = (int *)malloc(sizeof(int) * capacity);
    }

    printf("Rank %d: Broadcasting automaton data...\n", rank);
    // Prevents signed 32-bit integer overflow for large structures
    const size_t MAX_MPI_CHUNK_SIZE = 512 * 1024 * 1024; // 512 MB safe blocks

    // Broadcast the flat state array
    size_t states_bytes_left = (size_t)capacity * sizeof(ACState);
    size_t states_offset = 0;
    while (states_bytes_left > 0) {
        int chunk = (states_bytes_left > MAX_MPI_CHUNK_SIZE) ? (int)MAX_MPI_CHUNK_SIZE : (int)states_bytes_left;
        MPI_Bcast((char *)ac->states + states_offset, chunk, MPI_BYTE, 1, MPI_COMM_WORLD);
        states_offset += chunk;
        states_bytes_left -= chunk;
    }

    // Broadcast the parallel output chains array
    size_t output_bytes_left = (size_t)capacity * sizeof(int);
    size_t output_offset = 0;
    while (output_bytes_left > 0) {
        int chunk = (output_bytes_left > MAX_MPI_CHUNK_SIZE) ? (int)MAX_MPI_CHUNK_SIZE : (int)output_bytes_left;
        MPI_Bcast((char *)ac->output_next + output_offset, chunk, MPI_BYTE, 1, MPI_COMM_WORLD);
        output_offset += chunk;
        output_bytes_left -= chunk;
    }

    // 2. Rank 1 generates and distributes packets
    printf("Rank %d: Distributing packets...\n", rank);
    int total_packets = 1000000;
    // Ensure total_packets is divisible by size to simplify distribution
    total_packets -= (total_packets % size);
    int packets_per_proc = total_packets / size;
    Packet *my_packets = (Packet *)malloc(packets_per_proc * sizeof(Packet));
    Packet *all_packets = NULL;

    if (rank == 1) {
        printf("Rank 1: Generating %d packets...\n", total_packets);
        LengthDistParams lp = { 7.0, 1.0, 64, 8192 };
        all_packets = generate_packets(total_packets, 42, lp, 0.5, (const char **)patterns, num_patterns);

        for (int i = 0; i < size; i++) {
            if (i == 1) {
                for (int j = 0; j < packets_per_proc; j++) my_packets[j] = all_packets[i * packets_per_proc + j];
            } else {
                for (int j = 0; j < packets_per_proc; j++) {
                    Packet p = all_packets[i * packets_per_proc + j];
                    MPI_Send(&p.len, 1, MPI_UNSIGNED_LONG, i, 0, MPI_COMM_WORLD);
                    MPI_Send(&p.has_xss, 1, MPI_INT, i, 1, MPI_COMM_WORLD);
                    MPI_Send(p.data, p.len, MPI_BYTE, i, 2, MPI_COMM_WORLD);
                }
            }
        }
    } else {
        for (int j = 0; j < packets_per_proc; j++) {
            MPI_Recv(&my_packets[j].len, 1, MPI_UNSIGNED_LONG, 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&my_packets[j].has_xss, 1, MPI_INT, 1, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            my_packets[j].data = (uint8_t *)malloc(my_packets[j].len);
            MPI_Recv(my_packets[j].data, my_packets[j].len, MPI_BYTE, 1, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
    }

    printf("Rank %d: Starting scan...\n", rank);
    MPI_Barrier(MPI_COMM_WORLD);
    double scan_start = MPI_Wtime();
    
    uint64_t my_matches = 0;
    ACMatchList ml;
    ac_matchlist_init(&ml, 16);

    for (int i = 0; i < packets_per_proc; i++) {
        ac_scan_into(ac, my_packets[i].data, my_packets[i].len, &ml);
        my_matches += ml.count;
    }
    
    ac_free_matches(&ml);
    MPI_Barrier(MPI_COMM_WORLD);
    double scan_end = MPI_Wtime();
    
    uint64_t total_matches = 0;
    MPI_Reduce(&my_matches, &total_matches, 1, MPI_UNSIGNED_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        double scan_time = scan_end - scan_start;
        double total_bytes = (double)total_packets * 1024.0; // Approximation as in benchmark_ac_t.c
        double throughput_mb_per_sec = (total_bytes / 1e6) / scan_time;
        double packets_per_sec = (double)total_packets / scan_time;
        double avg_time_per_packet_us = (scan_time / total_packets) * 1e6;
        double avg_time_per_byte_ns = (scan_time / total_bytes) * 1e9;

        printf("  Scan time: %.6f sec\n", scan_time);
        printf("  Total matches found: %llu\n\n", total_matches);
        printf("Performance Metrics\n");
        printf("  ================================\n");
        printf("  Throughput:        %.2f MB/s\n", throughput_mb_per_sec);
        printf("  Packets/sec:       %.0f\n", packets_per_sec);
        printf("  Avg time/packet:   %.3f µs\n", avg_time_per_packet_us);
        printf("  Avg time/byte:     %.2f ns\n", avg_time_per_byte_ns);
        printf("  ================================\n\n");

        char filename[128];
        snprintf(filename, sizeof(filename), "multiprocess_benchmark_%dp.txt", size);
        printf("Saving results to %s...\n", filename);
        FILE *f = fopen(filename, "w");
        if (f) {
            fprintf(f, "=== Pattern Matching Multiprocess Performance Report (%d processes) ===\n\n", size);
            fprintf(f, "Results:\n");
            fprintf(f, "  Scan time:             %.6f sec\n", scan_time);
            fprintf(f, "  Total matches:         %llu\n", total_matches);
            fprintf(f, "  Throughput:            %.2f MB/s\n", throughput_mb_per_sec);
            fprintf(f, "  Packets/sec:           %.0f\n", packets_per_sec);
            fprintf(f, "  Avg time/packet:       %.3f µs\n", avg_time_per_packet_us);
            fprintf(f, "  Avg time/byte:         %.2f ns\n", avg_time_per_byte_ns);
            fclose(f);
        }
    }

    // Cleanup
    if (rank != 1) {
        for (int j = 0; j < packets_per_proc; j++) free(my_packets[j].data);
        free(ac->states);
        free(ac->output_next);
    } else {
        free_packets(all_packets, total_packets);
    }
    free(my_packets);
    ac_free(ac);
    MPI_Finalize();
    return 0;
}
