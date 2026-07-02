#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "config.h"
#include "dataset.h"
#include "pattern_matching.h"
#include "performance.h"

#ifdef HAVE_MPI
#include <mpi.h>
#endif

#ifdef _OPENMP
#include <omp.h>
#endif

// Helper to load patterns
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

// Helper to load dataset shards
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
            fread((*packets)[populated].data, 1, p_size, f);
            populated++;
        } else {
            fseek(f, p_size, SEEK_CUR);
        }
        current_idx++;
        if (populated == count) break;
    }
    fclose(f);
}

// Strategies
void run_sequential(const Config *config, ACAutomaton *ac, Packet *packets, uint32_t num_packets, PerformanceMetrics *metrics) {
    long matches = 0;
    long bytes_scanned = 0;
    double start_time = MPI_Wtime();
    ACMatchList ml;
    ac_matchlist_init(&ml, 64);
    for (uint32_t i = 0; i < num_packets; i++) {
        ac_scan_into(ac, (const uint8_t *)packets[i].data, packets[i].len, &ml);
        matches += ml.count;
        bytes_scanned += packets[i].len;
    }
    ac_free_matches(&ml);
    double elapsed = MPI_Wtime() - start_time;
    compute_metrics(metrics, elapsed, bytes_scanned, elapsed, 1);
}

void run_omp(const Config *config, ACAutomaton *ac, Packet *packets, uint32_t num_packets, PerformanceMetrics *metrics) {
#ifdef _OPENMP
    long matches = 0;
    long bytes_scanned = 0;
    omp_sched_t selected_sched = omp_sched_dynamic;
    if (strcmp(config->schedule_type, "guided") == 0) selected_sched = omp_sched_guided;
    else if (strcmp(config->schedule_type, "static") == 0) selected_sched = omp_sched_static;
    omp_set_schedule(selected_sched, (int)config->schedule_chunk);
    double start_time = MPI_Wtime();
    #pragma omp parallel num_threads(config->num_omp_threads) reduction(+:matches, bytes_scanned)
    {
        ACMatchList ml;
        ac_matchlist_init(&ml, 64);
        #pragma omp for schedule(runtime)
        for (uint32_t i = 0; i < num_packets; i++) {
            ac_scan_into(ac, (const uint8_t *)packets[i].data, packets[i].len, &ml);
            matches += ml.count;
            bytes_scanned += packets[i].len;
        }
        ac_free_matches(&ml);
    }
    double elapsed = MPI_Wtime() - start_time;
    compute_metrics(metrics, elapsed, bytes_scanned, elapsed, config->num_omp_threads);
#else
    fprintf(stderr, "OMP not supported.\n");
#endif
}

void run_mpi(const Config *config, ACAutomaton *ac, Packet *packets, uint32_t num_packets, PerformanceMetrics *metrics, int rank, int num_ranks, double baseline_time) {
#ifdef HAVE_MPI
    long matches = 0;
    long bytes_scanned = 0;
    double start_time = MPI_Wtime();
    ACMatchList ml;
    ac_matchlist_init(&ml, 64);
    for (uint32_t i = 0; i < num_packets; i++) {
        ac_scan_into(ac, (const uint8_t *)packets[i].data, packets[i].len, &ml);
        matches += ml.count;
        bytes_scanned += packets[i].len;
    }
    ac_free_matches(&ml);
    double local_elapsed = MPI_Wtime() - start_time;
    long g_matches = 0, g_bytes = 0;
    double max_time = 0.0;
    MPI_Reduce(&matches, &g_matches, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&bytes_scanned, &g_bytes, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_elapsed, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    if (rank == 0) compute_metrics(metrics, max_time, g_bytes, baseline_time, num_ranks);
#endif
}

void run_hybrid(const Config *config, ACAutomaton *ac, Packet *packets, uint32_t num_packets, PerformanceMetrics *metrics, int rank, int num_ranks, double baseline_time) {
#if defined(HAVE_MPI) && defined(_OPENMP)
    long matches = 0;
    long bytes_scanned = 0;
    omp_sched_t selected_sched = omp_sched_dynamic;
    if (strcmp(config->schedule_type, "guided") == 0) selected_sched = omp_sched_guided;
    else if (strcmp(config->schedule_type, "static") == 0) selected_sched = omp_sched_static;
    omp_set_schedule(selected_sched, (int)config->schedule_chunk);
    double start_time = MPI_Wtime();
    #pragma omp parallel num_threads(config->num_omp_threads) reduction(+:matches, bytes_scanned)
    {
        ACMatchList ml;
        ac_matchlist_init(&ml, 64);
        #pragma omp for schedule(runtime)
        for (uint32_t i = 0; i < num_packets; i++) {
            ac_scan_into(ac, (const uint8_t *)packets[i].data, packets[i].len, &ml);
            matches += ml.count;
            bytes_scanned += packets[i].len;
        }
        ac_free_matches(&ml);
    }
    double local_elapsed = MPI_Wtime() - start_time;
    long g_matches = 0, g_bytes = 0;
    double max_time = 0.0;
    MPI_Reduce(&matches, &g_matches, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&bytes_scanned, &g_bytes, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_elapsed, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    if (rank == 0) compute_metrics(metrics, max_time, g_bytes, baseline_time, num_ranks * config->num_omp_threads);
#endif
}

int main(int argc, char *argv[]) {
    Config config;
    init_default_config(&config);
    parse_arguments(argc, argv, &config);
    int rank = 0, num_ranks = 1, mpi_initialized = 0;
    int is_mpi_strategy = (strcmp(config.strategy_type, "mpi") == 0 || strcmp(config.strategy_type, "hybrid") == 0 || strcmp(config.strategy_type, "all") == 0);
#ifdef HAVE_MPI
    if (is_mpi_strategy) {
        int mpi_provided;
        MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &mpi_provided);
        mpi_initialized = 1;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        MPI_Comm_size(MPI_COMM_WORLD, &num_ranks);
        config.num_mpi_ranks = num_ranks;
    }
#endif
    if (rank == 0 && !validate_config(&config)) exit(EXIT_FAILURE);
    char **patterns = NULL;
    int pattern_count = 0;
    ACAutomaton *ac = NULL;
    if (rank == 0) {
        pattern_count = load_patterns_from_file(config.pattern_file, &patterns);
        if (pattern_count < 0) exit(EXIT_FAILURE);
        ac = ac_build((const char **)patterns, pattern_count);
        config.num_patterns = (uint32_t)pattern_count;
    }
#ifdef HAVE_MPI
    if (is_mpi_strategy) {
        MPI_Bcast(&config, sizeof(Config), MPI_BYTE, 0, MPI_COMM_WORLD);
        int num_states = (rank == 0) ? ac->num_states : 0;
        int capacity = (rank == 0) ? ac->capacity : 0;
        MPI_Bcast(&pattern_count, 1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Bcast(&num_states, 1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Bcast(&capacity, 1, MPI_INT, 0, MPI_COMM_WORLD);
        if (rank != 0) {
            config.num_patterns = (uint32_t)pattern_count;
            ac = (ACAutomaton *)malloc(sizeof(ACAutomaton));
            ac->num_states = num_states;
            ac->capacity = capacity;
            ac->num_patterns = pattern_count;
            ac->states = (ACState *)malloc(sizeof(ACState) * capacity);
            ac->output_next = (int *)malloc(sizeof(int) * capacity);
        }
        
        const size_t MAX_MPI_CHUNK_SIZE = 512 * 1024 * 1024;
        
        size_t states_bytes_left = (size_t)capacity * sizeof(ACState);
        size_t states_offset = 0;
        while (states_bytes_left > 0) {
            int chunk = (states_bytes_left > MAX_MPI_CHUNK_SIZE) ? (int)MAX_MPI_CHUNK_SIZE : (int)states_bytes_left;
            MPI_Bcast((char *)ac->states + states_offset, chunk, MPI_BYTE, 0, MPI_COMM_WORLD);
            states_offset += chunk;
            states_bytes_left -= chunk;
        }
        
        size_t output_bytes_left = (size_t)capacity * sizeof(int);
        size_t output_offset = 0;
        while (output_bytes_left > 0) {
            int chunk = (output_bytes_left > MAX_MPI_CHUNK_SIZE) ? (int)MAX_MPI_CHUNK_SIZE : (int)output_bytes_left;
            MPI_Bcast((char *)ac->output_next + output_offset, chunk, MPI_BYTE, 0, MPI_COMM_WORLD);
            output_offset += chunk;
            output_bytes_left -= chunk;
        }
    }
#endif
    Packet *local_packets = NULL;
    uint32_t num_packets_local = 0;
    load_binary_dataset_shard(config.dataset_file, rank, num_ranks, &local_packets, &num_packets_local);
    PerformanceMetrics m_seq, m_omp, m_mpi, m_hybrid;
    memset(&m_seq, 0, sizeof(PerformanceMetrics));
    memset(&m_omp, 0, sizeof(PerformanceMetrics));
    memset(&m_mpi, 0, sizeof(PerformanceMetrics));
    memset(&m_hybrid, 0, sizeof(PerformanceMetrics));
    
    int run_all = (strcmp(config.strategy_type, "all") == 0);
    
    if (run_all || strcmp(config.strategy_type, "sequential") == 0) {
        run_sequential(&config, ac, local_packets, num_packets_local, &m_seq);
        if (rank == 0) print_performance_report(&config, "sequential", &m_seq, 1);
    }
    if (run_all || strcmp(config.strategy_type, "omp") == 0) {
        run_omp(&config, ac, local_packets, num_packets_local, &m_omp);
        if (rank == 0) print_performance_report(&config, "omp", &m_omp, config.num_omp_threads);
    }
    if (is_mpi_strategy && (run_all || strcmp(config.strategy_type, "mpi") == 0)) {
        run_mpi(&config, ac, local_packets, num_packets_local, &m_mpi, rank, num_ranks, m_seq.exec_time > 0 ? m_seq.exec_time : 1.0);
        if (rank == 0) print_performance_report(&config, "mpi", &m_mpi, num_ranks);
    }
    if (is_mpi_strategy && (run_all || strcmp(config.strategy_type, "hybrid") == 0)) {
        run_hybrid(&config, ac, local_packets, num_packets_local, &m_hybrid, rank, num_ranks, m_seq.exec_time > 0 ? m_seq.exec_time : 1.0);
        if (rank == 0) print_performance_report(&config, "hybrid", &m_hybrid, num_ranks * config.num_omp_threads);
    }
    
    if (rank == 0 && run_all) {
        print_comparison(&m_seq, &m_omp, &m_mpi, &m_hybrid);
    }
    
    for (uint32_t i = 0; i < num_packets_local; i++) free(local_packets[i].data);
    free(local_packets);
    if (patterns) {
        for (int i = 0; i < pattern_count; i++) free(patterns[i]);
        free(patterns);
    }
    if (ac) {
        if (rank != 0) { free(ac->states); free(ac->output_next); }
        ac_free(ac);
    }
#ifdef HAVE_MPI
    if (mpi_initialized) MPI_Finalize();
#endif
    return 0;
}
