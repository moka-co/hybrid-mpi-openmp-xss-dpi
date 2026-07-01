#include <mpi.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // For access()
#include "config.h"
#include "dataset.h"
#include "pattern_matching.h"

// 5.1 Define schema in C as a struct
typedef struct {
    double exec_time;
    double throughput_mb_s;
    double speedup;
    double efficiency;
} PerformanceMetrics;

// Helper function to load patterns from a text file cleanly on all ranks
static int load_patterns_from_file(const char *filename, char ***patterns_out) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    char **patterns = NULL;
    int count = 0;
    char line[256];

    while (fgets(line, sizeof(line), f)) {
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

    uint32_t total_packets = 0;
    uint32_t p_size;
    while (fread(&p_size, sizeof(uint32_t), 1, f) == 1) {
        fseek(f, p_size, SEEK_CUR);
        total_packets++;
    }

    uint32_t count = total_packets / num_ranks;
    int remainder = total_packets % num_ranks;
    uint32_t start_idx = rank * count + (rank < remainder ? rank : remainder);
    if (rank < remainder) count++;

    *local_count = count;
    *packets = malloc(count * sizeof(Packet));

    rewind(f);
    uint32_t current_idx = 0;
    uint32_t populated = 0;

    while (fread(&p_size, sizeof(uint32_t), 1, f) == 1) {
        if (current_idx >= start_idx && populated < count) {
            (*packets)[populated].len = p_size;
            (*packets)[populated].data = malloc(p_size);
            size_t read_bytes = fread((*packets)[populated].data, 1, p_size, f);
            (void)read_bytes;
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
    int mpi_provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &mpi_provided);

    int rank, num_ranks;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_ranks);

    Config config;
    SystemMetadata metadata;

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

    MPI_Bcast(&config, sizeof(Config), MPI_BYTE, 0, MPI_COMM_WORLD);

    #ifdef _OPENMP
    omp_sched_t selected_sched = omp_sched_dynamic;
    if (strcmp(config.schedule_type, "guided") == 0) {
        selected_sched = omp_sched_guided;
    } else if (strcmp(config.schedule_type, "static") == 0) {
        selected_sched = omp_sched_static;
    }
    omp_set_schedule(selected_sched, (int)config.schedule_chunk);
    #endif

<<<<<<< Updated upstream
    // 3. Load Pattern Signatures & Build Aho-Corasick Automaton locally on all ranks
=======
    printf("Building the Automaton...\n");
>>>>>>> Stashed changes
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

<<<<<<< Updated upstream
    // 4. Shard Loading from the real generated binary packet file
=======
    Printf("Automaton Build!\n");

>>>>>>> Stashed changes
    Packet *local_packets = NULL;
    uint32_t num_packets_local = 0;
    load_binary_dataset_shard("datasets/packets.bin", rank, num_ranks, &local_packets, &num_packets_local);

    uint32_t global_packet_total = 0;
    MPI_Allreduce(&num_packets_local, &global_packet_total, 1, MPI_UINT32_T, MPI_SUM, MPI_COMM_WORLD);
    config.packet_count = global_packet_total;

    // ==========================================
    // PATH 1: Pure Sequential Baseline
    // ==========================================
    long seq_matches = 0;
    long seq_bytes_scanned = 0;

    ACMatchList seq_ml; 
    ac_matchlist_init(&seq_ml, 16); //initialize with initial capacity 32

    MPI_Barrier(MPI_COMM_WORLD);
    double seq_start_time = MPI_Wtime();

    for (uint32_t i = 0; i < num_packets_local; i++) {
        ac_scan_into(ac, (const uint8_t *)local_packets[i].data, local_packets[i].len, &seq_ml);
        seq_matches += seq_ml.count;
        seq_bytes_scanned += local_packets[i].len;
    }

    double seq_end_time = MPI_Wtime();
    double seq_local_elapsed = seq_end_time - seq_start_time;

    // ==========================================
    // PATH 2: VERSION A — Naive Static Schedule
    // ==========================================
    long a_matches = 0;
    long a_bytes_scanned = 0;

    MPI_Barrier(MPI_COMM_WORLD);
    double a_start_time = MPI_Wtime();

    #pragma omp parallel num_threads(config.num_omp_threads)
    {
        ACMatchList a_ml;
        ac_matchlist_init(&a_ml, 16); // once per thread, not per packet

        #pragma omp for schedule(static) reduction(+:a_matches, a_bytes_scanned)
        for (uint32_t i = 0; i < num_packets_local; i++) {
            ac_scan_into(ac, (const uint8_t *)local_packets[i].data, local_packets[i].len, &a_ml);
            a_matches += a_ml.count;
            a_bytes_scanned += local_packets[i].len;
        }

        ac_free_matches(&a_ml); // once per thread, at the end
    }

    double a_end_time = MPI_Wtime();
    double a_local_elapsed = a_end_time - a_start_time;

    // ==========================================
    // PATH 3: VERSION B — Optimized Dynamic/Guided Schedule
    // ==========================================
    long b_matches = 0;
    long b_bytes_scanned = 0;

    MPI_Barrier(MPI_COMM_WORLD);
    double b_start_time = MPI_Wtime();

    #pragma omp parallel for \
        schedule(runtime) \
        num_threads(config.num_omp_threads) \
        reduction(+:b_matches, b_bytes_scanned)
    for (uint32_t i = 0; i < num_packets_local; i++) {
        ACMatchList ml = ac_scan(ac, (const uint8_t *)local_packets[i].data, local_packets[i].len);
        b_matches += ml.count;
        b_bytes_scanned += local_packets[i].len;
        ac_free_matches(&ml);
    }

    double b_end_time = MPI_Wtime();
    double b_local_elapsed = b_end_time - b_start_time;

    // ==========================================
    // 5. Global Metrics Reduction & Aggregation
    // ==========================================
    long g_seq_matches = 0, g_a_matches = 0, g_b_matches = 0;
    long g_seq_bytes = 0, g_a_bytes = 0, g_b_bytes = 0;
    double max_seq_time = 0.0, max_a_time = 0.0, max_b_time = 0.0;

    MPI_Reduce(&seq_matches, &g_seq_matches, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&seq_bytes_scanned, &g_seq_bytes, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&seq_local_elapsed, &max_seq_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    MPI_Reduce(&a_matches, &g_a_matches, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&a_bytes_scanned, &g_a_bytes, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&a_local_elapsed, &max_a_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    MPI_Reduce(&b_matches, &g_b_matches, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&b_bytes_scanned, &g_b_bytes, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&b_local_elapsed, &max_b_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    // ==========================================
    // 5.2 Implement metric computation in C
    // ==========================================
    PerformanceMetrics metrics_seq, metrics_a, metrics_b;
    double total_hardware_workers = (double)(num_ranks * config.num_omp_threads);

    // Baseline calculation (Sequential)
    metrics_seq.exec_time = max_seq_time;
    metrics_seq.throughput_mb_s = ((double)g_seq_bytes / 1e6) / max_seq_time;
    metrics_seq.speedup = 1.0;
    metrics_seq.efficiency = 1.0 / total_hardware_workers;

    // Version A Metrics
    metrics_a.exec_time = max_a_time;
    metrics_a.throughput_mb_s = ((double)g_a_bytes / 1e6) / max_a_time;
    metrics_a.speedup = max_seq_time / max_a_time;
    metrics_a.efficiency = metrics_a.speedup / total_hardware_workers;

    // Version B Metrics
    metrics_b.exec_time = max_b_time;
    metrics_b.throughput_mb_s = ((double)g_b_bytes / 1e6) / max_b_time;
    metrics_b.speedup = max_seq_time / max_b_time;
    metrics_b.efficiency = metrics_b.speedup / total_hardware_workers;

    // ==========================================
    // 6. Comparative Performance Report Output & CSV Export
    // ==========================================
    if (rank == 0) {
        printf("=== Performance Evaluation Report ===\n");
        printf("  Total MPI Ranks Active: %d\n", num_ranks);
        printf("  OMP Threads per Rank:   %u\n", config.num_omp_threads);
        printf("  Version B Schedule:     %s (Chunk: %u)\n", config.schedule_type, config.schedule_chunk);
        printf("  Loaded Signatures Count:%u\n", config.num_patterns);
        printf("  Global Packets Scanned: %u\n\n", config.packet_count);

        printf("  [SEQUENTIAL BASELINE]\n");
        printf("    Max Wall Clock Time:  %f seconds\n", metrics_seq.exec_time);
        printf("    Throughput:           %f MB/s\n\n", metrics_seq.throughput_mb_s);

        printf("  [VERSION A - static schedule]\n");
        printf("    Max Wall Clock Time:  %f seconds\n", metrics_a.exec_time);
        printf("    Throughput:           %f MB/s\n", metrics_a.throughput_mb_s);
        printf("    Calculated Speedup:   %fx\n", metrics_a.speedup);
        printf("    Parallel Efficiency:  %f\n\n", metrics_a.efficiency);

        printf("  [VERSION B - %s schedule]\n", config.schedule_type);
        printf("    Max Wall Clock Time:  %f seconds\n", metrics_b.exec_time);
        printf("    Throughput:           %f MB/s\n", metrics_b.throughput_mb_s);
        printf("    Calculated Speedup:   %fx\n", metrics_b.speedup);
        printf("    Parallel Efficiency:  %f\n\n", metrics_b.efficiency);
        
        printf("  [EFFICIENCY METRICS]\n");
        printf("    Speedup B vs A:          %fx   <-- headline A/B result\n", max_a_time / max_b_time);
        printf("======================================\n");

        if (g_seq_matches != g_a_matches || g_seq_matches != g_b_matches) {
            fprintf(stderr, "WARNING: match-count mismatch across versions.\n");
        }

        // Root rank appends row to CSV file
        if (config.output_file[0] != '\0' && strcmp(config.output_format, "csv") == 0) {
            int file_exists = (access(config.output_file, F_OK) == 0);
            FILE *csv = fopen(config.output_file, "a");
            
            if (csv) {
                // Write CSV header if creating a new file
                if (!file_exists) {
                    fprintf(csv, "mpi_ranks,omp_threads,b_schedule_type,b_schedule_chunk,global_packets,"
                                 "seq_time,seq_throughput_mbs,seq_speedup,seq_efficiency,"
                                 "a_time,a_throughput_mbs,a_speedup,a_efficiency,"
                                 "b_time,b_throughput_mbs,b_speedup,b_efficiency\n");
                }
                // Append row containing exact metrics computed above
                fprintf(csv, "%d,%u,%s,%u,%u,"
                             "%f,%f,%f,%f,"
                             "%f,%f,%f,%f,"
                             "%f,%f,%f,%f\n",
                        num_ranks, config.num_omp_threads, config.schedule_type, config.schedule_chunk, config.packet_count,
                        metrics_seq.exec_time, metrics_seq.throughput_mb_s, metrics_seq.speedup, metrics_seq.efficiency,
                        metrics_a.exec_time, metrics_a.throughput_mb_s, metrics_a.speedup, metrics_a.efficiency,
                        metrics_b.exec_time, metrics_b.throughput_mb_s, metrics_b.speedup, metrics_b.efficiency);
                fclose(csv);
                printf("Metrics appended to CSV logging sink: %s\n", config.output_file);
            } else {
                fprintf(stderr, "Error: Failed to write metrics to CSV destination path: %s\n", config.output_file);
            }
        }
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