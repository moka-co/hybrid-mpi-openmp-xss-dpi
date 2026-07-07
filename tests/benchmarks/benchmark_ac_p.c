// tests/benchmarks/benchmark_ac_p.c
//
// File: benchmark_ac_p.c
// Description: MPI-parallelized performance benchmark for Aho-Corasick pattern matching.
//
// Compile: mpicc -O3 -Wall -Isrc/ -o tests/benchmarks/benchmark_ac_p tests/benchmarks/benchmark_ac_p.c src/pattern_matching.c src/dataset.c
// Run: mpirun -np <num_ranks> ./tests/benchmarks/benchmark_ac_p

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <mpi.h>
#include "../../src/pattern_matching.h"
#include "../../src/dataset.h"
#include "../../src/config.h"

#define MASTER_RANK 0

/**
 * Main execution: Runs MPI-parallelized Aho-Corasick benchmark.
 */
int main(int argc, char *argv[])
{
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    Config cfg;
    if (rank == MASTER_RANK) {
        // Note: Use command-line arguments to override defaults (e.g., --num-packets <num>)
        init_default_config(&cfg);
        parse_arguments(argc, argv, &cfg);
        cfg.num_mpi_ranks = size;
    }
    MPI_Bcast(&cfg, sizeof(Config), MPI_BYTE, MASTER_RANK, MPI_COMM_WORLD);

    int num_patterns = 0;
    char **patterns = NULL;
    ACAutomaton *ac = NULL;
    int num_states = 0, capacity = 0;

    // 1. Rank MASTER_RANK builds automaton and broadcasts it
    if (rank == MASTER_RANK) {
        printf("Rank %d: Building automaton (patterns: %s)...\n", MASTER_RANK, cfg.pattern_file);
        patterns = load_patterns_from_file(cfg.pattern_file, &num_patterns);
        ac = ac_build((const char **)patterns, num_patterns);
        num_states = ac->num_states;
        capacity = ac->capacity;
    }

    MPI_Bcast(&num_patterns, 1, MPI_INT, MASTER_RANK, MPI_COMM_WORLD);
    MPI_Bcast(&num_states, 1, MPI_INT, MASTER_RANK, MPI_COMM_WORLD);
    MPI_Bcast(&capacity, 1, MPI_INT, MASTER_RANK, MPI_COMM_WORLD);

    if (rank != MASTER_RANK) {
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
        MPI_Bcast((char *)ac->states + states_offset, chunk, MPI_BYTE, MASTER_RANK, MPI_COMM_WORLD);
        states_offset += chunk;
        states_bytes_left -= chunk;
    }

    // Broadcast the parallel output chains array
    size_t output_bytes_left = (size_t)capacity * sizeof(int);
    size_t output_offset = 0;
    while (output_bytes_left > 0) {
        int chunk = (output_bytes_left > MAX_MPI_CHUNK_SIZE) ? (int)MAX_MPI_CHUNK_SIZE : (int)output_bytes_left;
        MPI_Bcast((char *)ac->output_next + output_offset, chunk, MPI_BYTE, MASTER_RANK, MPI_COMM_WORLD);
        output_offset += chunk;
        output_bytes_left -= chunk;
    }

    // 2. Rank MASTER_RANK generates and distributes packets
    printf("Rank %d: Distributing packets...\n", rank);
    int total_packets = (int)cfg.packet_count;
    // Ensure total_packets is divisible by size to simplify distribution
    total_packets -= (total_packets % size);
    int packets_per_proc = total_packets / size;
    Packet *my_packets = (Packet *)malloc(packets_per_proc * sizeof(Packet));
    Packet *all_packets = NULL;

    if (rank == MASTER_RANK) {
        printf("Rank %d: Generating %d packets...\n", MASTER_RANK, total_packets);
        LengthDistParams lp = { 7.0, 1.0, 64, 8192 };
        all_packets = generate_packets(total_packets, 42, lp, 0.5, (const char **)patterns, num_patterns);

        for (int i = 0; i < size; i++) {
            if (i == MASTER_RANK) {
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
            MPI_Recv(&my_packets[j].len, 1, MPI_UNSIGNED_LONG, MASTER_RANK, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&my_packets[j].has_xss, 1, MPI_INT, MASTER_RANK, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            my_packets[j].data = (uint8_t *)malloc(my_packets[j].len);
            MPI_Recv(my_packets[j].data, my_packets[j].len, MPI_BYTE, MASTER_RANK, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
    }

    printf("Rank %d: Scanning packets (measuring time)...\n", rank);
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

        // Generate JSON
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
            "    \"processes\": %d,\n"
            "    \"threads\": %d,\n"
            "    \"scheduler\": \"%s\"\n"
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
            num_patterns, num_states, total_packets, total_bytes / 1e6, total_bytes / total_packets, cfg.num_mpi_ranks, cfg.num_omp_threads, cfg.strategy_type,
            scan_time, (unsigned long)total_matches, throughput_mb_per_sec, packets_per_sec, avg_time_per_packet_us, avg_time_per_byte_ns,
            num_states, total_bytes / num_states, sizeof(ACState), AC_ALPHABET_SIZE * sizeof(int));

        // Print to stdout
        printf("%s\n", json_buffer);

        // Save to file
        char filename[64];
        snprintf(filename, sizeof(filename), "results/benchmark_ac_p%d_static.json", size);
        FILE *f = fopen(filename, "w");
        if (f) {
            fprintf(f, "%s\n", json_buffer);
            fclose(f);
        }
        printf("=== Benchmark Complete ===\n");
    }

    // Cleanup
    if (rank != MASTER_RANK) {
        for (int j = 0; j < packets_per_proc; j++) free(my_packets[j].data);
        free(ac->states);
        free(ac->output_next);
    } else {
        free_packets(all_packets, total_packets);
        free_patterns_list(patterns, num_patterns);
    }
    free(my_packets);
    ac_free(ac);
    MPI_Finalize();
    return 0;
}
