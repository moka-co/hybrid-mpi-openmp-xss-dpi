#include <mpi.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "dataset.h"
#include "pattern_matching.h"

// Helper function to load patterns from a text file cleanly on all ranks
static int load_patterns_from_file(const char *filename, char ***patterns_out) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    char **patterns = NULL;
    int count = 0;
    char line[256];

    while (fgets(line, sizeof(line), f)) {
        // Strip trailing newline characters safely
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) == 0) continue;

        char **tmp = realloc(patterns, (count + 1) * sizeof(char *));
        if (!tmp) {
            for (int i = 0; i < count; i++) free(patterns[i]);
            free(patterns);
            fclose(f);
            return -1;
        }
        patterns = tmp;
        patterns[count] = strdup(line);
        count++;
    }

    fclose(f);
    *patterns_out = patterns;
    return count;
}

// Reads the actual datasets/packets.bin file and distributes shards to MPI ranks
void load_binary_dataset_shard(const char *filename, int rank, int num_ranks, Packet **packets, uint32_t *local_count) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "[Rank %d] Error: Cannot open dataset file %s\n", rank, filename);
        *local_count = 0;
        *packets = NULL;
        return;
    }

    // 1. Scan the layout to count total packets in the binary file
    uint32_t total_packets = 0;
    uint32_t p_size;
    while (fread(&p_size, sizeof(uint32_t), 1, f) == 1) {
        fseek(f, p_size, SEEK_CUR);
        total_packets++;
    }

    // 2. Calculate this rank's allocation share bounds
    uint32_t count = total_packets / num_ranks;
    int remainder = total_packets % num_ranks;
    uint32_t start_idx = rank * count + (rank < remainder ? rank : remainder);
    if (rank < remainder) count++;

    *local_count = count;
    *packets = malloc(count * sizeof(Packet));

    // 3. Rewind and extract this rank's specific subset of packets
    rewind(f);
    uint32_t current_idx = 0;
    uint32_t populated = 0;

    while (fread(&p_size, sizeof(uint32_t), 1, f) == 1) {
        if (current_idx >= start_idx && populated < count) {
            (*packets)[populated].len = p_size; 
            (*packets)[populated].data = malloc(p_size);
            size_t read_bytes = fread((*packets)[populated].data, 1, p_size, f);
            (void)read_bytes; // Suppress unused result warning
            populated++;
        } else {
            fseek(f, p_size, SEEK_CUR);
        }
        current_idx++;
        if (populated == count) break;
    }
    fclose(f);
}

int main(int argc, char *argv[]) {
    // 1. Initialize MPI Environment
    int mpi_provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &mpi_provided);

    int rank, num_ranks;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_ranks);

    Config config;
    SystemMetadata metadata;

    // 2. Configuration Parsing & Validation
    init_default_config(&config);
    parse_arguments(argc, argv, &config);
    config.num_mpi_ranks = num_ranks;

    if (rank == 0) {
        if (!validate_config(&config)) {
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            exit(EXIT_FAILURE);
        }
        print_config(&config);
        
        capture_metadata(&metadata);
        printf("\n=== Metadata Environment Verification ===\n");
        printf("Host: %s | GCC: %s | MPI: %s | Kernel: %s\n\n", 
               metadata.hostname, metadata.gcc_version, metadata.mpi_version, metadata.sys_info);
    }

    // Broadcast valid configurations to all worker ranks
    MPI_Bcast(&config, sizeof(Config), MPI_BYTE, 0, MPI_COMM_WORLD);

    // Apply Dynamic OpenMP runtime scheduling configuration adjustments
    #ifdef _OPENMP
    omp_sched_t selected_sched = omp_sched_static;
    if (strcmp(config.schedule_type, "dynamic") == 0) {
        selected_sched = omp_sched_dynamic;
    } else if (strcmp(config.schedule_type, "guided") == 0) {
        selected_sched = omp_sched_guided;
    }
    omp_set_schedule(selected_sched, (int)config.schedule_chunk);
    #endif

    // 3. Load Pattern Signatures & Build Aho-Corasick Automaton locally on all ranks
    char **patterns = NULL;
    int pattern_count = load_patterns_from_file(config.pattern_file, &patterns);
    if (pattern_count < 0) {
        fprintf(stderr, "[Rank %d] Error: Failed to open or read pattern file: %s\n", rank, config.pattern_file);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit(EXIT_FAILURE);
    }

    ACAutomaton *ac = ac_build((const char **)patterns, pattern_count);
    if (!ac) {
        fprintf(stderr, "[Rank %d] Error: Failed to construct Aho-Corasick automaton\n", rank);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit(EXIT_FAILURE);
    }

    config.num_patterns = (uint32_t)pattern_count;

    // 4. Shard Loading from the real generated binary packet file
    Packet *local_packets = NULL;
    uint32_t num_packets_local = 0;
    load_binary_dataset_shard("datasets/packets.bin", rank, num_ranks, &local_packets, &num_packets_local);

    // Sync total parsed packet realities with our configuration counter metric
    uint32_t global_packet_total = 0;
    MPI_Allreduce(&num_packets_local, &global_packet_total, 1, MPI_UINT32_T, MPI_SUM, MPI_COMM_WORLD);
    config.packet_count = global_packet_total;

    // ==========================================
    // PATH 1: Pure Sequential Execution (Local Shard)
    // ==========================================
    long seq_matches = 0;
    long seq_bytes_scanned = 0;

    MPI_Barrier(MPI_COMM_WORLD);
    double seq_start_time = MPI_Wtime();

    for (uint32_t i = 0; i < num_packets_local; i++) {
        ACMatchList ml = ac_scan(ac, (const uint8_t *)local_packets[i].data, local_packets[i].len);
        seq_matches += ml.count;
        seq_bytes_scanned += local_packets[i].len;
        ac_free_matches(&ml);
    }

    double seq_end_time = MPI_Wtime();
    double seq_local_elapsed = seq_end_time - seq_start_time;

    // ==========================================
    // PATH 2: Parallel OpenMP Execution (Configured Schedule)
    // ==========================================
    long parallel_matches = 0;
    long parallel_bytes_scanned = 0;

    MPI_Barrier(MPI_COMM_WORLD);
    double parallel_start_time = MPI_Wtime();

    #pragma omp parallel for \
        schedule(runtime) \
        num_threads(config.num_omp_threads) \
        reduction(+:parallel_matches, parallel_bytes_scanned)
    for (uint32_t i = 0; i < num_packets_local; i++) {
        ACMatchList ml = ac_scan(ac, (const uint8_t *)local_packets[i].data, local_packets[i].len);
        parallel_matches += ml.count;
        parallel_bytes_scanned += local_packets[i].len;
        ac_free_matches(&ml);
    }

    double parallel_end_time = MPI_Wtime();
    double parallel_local_elapsed = parallel_end_time - parallel_start_time;

    // ==========================================
    // 5. Global Metrics Reduction & Aggregation
    // ==========================================
    long global_seq_matches = 0, global_parallel_matches = 0;
    long global_seq_bytes = 0, global_parallel_bytes = 0;
    double max_seq_time = 0.0, max_parallel_time = 0.0;

    MPI_Reduce(&seq_matches, &global_seq_matches, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&seq_bytes_scanned, &global_seq_bytes, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&seq_local_elapsed, &max_seq_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    MPI_Reduce(&parallel_matches, &global_parallel_matches, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&parallel_bytes_scanned, &global_parallel_bytes, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&parallel_local_elapsed, &max_parallel_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    // ==========================================
    // 6. Comparative Performance Report Output
    // ==========================================
    if (rank == 0) {
        printf("=== Performance Evaluation Report ===\n");
        printf("  Total MPI Ranks Active: %d\n", num_ranks);
        printf("  OMP Threads per Rank:   %u\n", config.num_omp_threads);
        printf("  OMP Schedule Type:      %s (Chunk: %u)\n", config.schedule_type, config.schedule_chunk);
        printf("  Loaded Signatures Count:%u\n", config.num_patterns);
        printf("  Global Packets Scanned: %u\n\n", config.packet_count);

        printf("  [SEQUENTIAL MODE]\n");
        printf("    Max Wall Clock Time:  %f seconds\n", max_seq_time);
        printf("    Total Bytes Scanned:  %ld Bytes\n", global_seq_bytes);
        printf("    Total Matches Found:  %ld\n", global_seq_matches);
        printf("    Throughput:           %f MB/s\n\n", 
               ((double)global_seq_bytes / (1024.0 * 1024.0)) / max_seq_time);

        printf("  [PARALLEL HYBRID MODE - %s]\n", config.schedule_type);
        printf("    Max Wall Clock Time:  %f seconds\n", max_parallel_time);
        printf("    Total Bytes Scanned:  %ld Bytes\n", global_parallel_bytes);
        printf("    Total Matches Found:  %ld\n", global_parallel_matches);
        printf("    Throughput:           %f MB/s\n\n", 
               ((double)global_parallel_bytes / (1024.0 * 1024.0)) / max_parallel_time);

        printf("  [EFFICIENCY METRICS]\n");
        printf("    Calculated Speedup:   %fx\n", max_seq_time / max_parallel_time);
        printf("======================================\n");
    }

    // Cleanup Local Allocations
    for (uint32_t i = 0; i < num_packets_local; i++) {
        free(local_packets[i].data);
    }
    free(local_packets);

    for (int i = 0; i < pattern_count; i++) {
        free(patterns[i]);
    }
    free(patterns);
    ac_free(ac);

    MPI_Finalize();
    return 0;
}