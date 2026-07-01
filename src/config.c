#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>

#ifdef HAVE_MPI
#include <mpi.h>
#endif

#ifdef _OPENMP
#include <omp.h>
#endif

void init_default_config(Config *cfg) {
    memset(cfg, 0, sizeof(Config));
    cfg->dataset_size = 1024 * 1024 * 1024; // 1GB default
    cfg->packet_count = 1000000;            // 1kk packets default
    strncpy(cfg->dataset_file, "datasets/packets.bin", sizeof(cfg->dataset_file) - 1);
    strncpy(cfg->pattern_file, "datasets/patterns.txt", sizeof(cfg->pattern_file) - 1);
    cfg->num_patterns = 0;
    cfg->num_mpi_ranks = 1;
    cfg->num_omp_threads = 4;              // 4 OMP threads default
    strncpy(cfg->schedule_type, "static", sizeof(cfg->schedule_type) - 1);
    cfg->schedule_chunk = 0;               // 0 means default chunk sizing
    strncpy(cfg->output_file, "results.csv", sizeof(cfg->output_file) - 1);
    strncpy(cfg->output_format, "csv", sizeof(cfg->output_format) - 1);
    cfg->num_repetitions = 1;
    cfg->random_seed = 42;                 // seed=42 default
    cfg->verbose = 0;
}

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [options]\n", prog_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --dataset-mb <num>   Dataset size in MB (default: 100)\n");
    fprintf(stderr, "  --num-packets <num>  Total number of packets (default: 1000000)\n");
    fprintf(stderr, "  --dataset-file <str> Path to binary packet dataset (default: datasets/packets.bin)\n");
    fprintf(stderr, "  --pattern-file <str> Path to signature pattern file (default: patterns.txt)\n");
    fprintf(stderr, "  --mpi-ranks <num>    Target MPI ranks (default: 1)\n");
    fprintf(stderr, "  --omp-threads <num>  Number of OpenMP threads (default: 4)\n");
    fprintf(stderr, "  --schedule <str>     OpenMP schedule: static, dynamic, guided (default: static)\n");
    fprintf(stderr, "  --seed <num>         Random seed generation index (default: 42)\n");
    fprintf(stderr, "  --output <str>       Output filepath metrics destination (default: results.csv)\n");
    fprintf(stderr, "  --format <str>       Output syntax format: csv, json (default: csv)\n");
    fprintf(stderr, "  --reps <num>         Number of execution iterations (default: 1)\n");
    fprintf(stderr, "  --verbose            Enable verbose debug logs\n");
}

void parse_arguments(int argc, char *argv[], Config *cfg) {
    int opt;
    int option_index = 0;

    static struct option long_options[] = {
        {"dataset-mb",   required_argument, 0, 'd'},
        {"num-packets",  required_argument, 0, 'p'},
        {"dataset-file", required_argument, 0, 'D'},
        {"pattern-file", required_argument, 0, 'f'},
        {"mpi-ranks",    required_argument, 0, 'm'},
        {"omp-threads",  required_argument, 0, 't'},
        {"schedule",     required_argument, 0, 's'},
        {"seed",         required_argument, 0, 'r'},
        {"output",       required_argument, 0, 'o'},
        {"format",       required_argument, 0, 'x'},
        {"reps",         required_argument, 0, 'n'},
        {"verbose",      no_argument,       0, 'v'},
        {"help",         no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "d:p:D:f:m:t:s:r:o:x:n:vh", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'd':
                cfg->dataset_size = (uint64_t)strtoull(optarg, NULL, 10) * 1024 * 1024;
                break;
            case 'p':
                cfg->packet_count = (uint32_t)strtoul(optarg, NULL, 10);
                break;
            case 'D':
                strncpy(cfg->dataset_file, optarg, sizeof(cfg->dataset_file) - 1);
                cfg->dataset_file[sizeof(cfg->dataset_file) - 1] = '\0';
                break;
            case 'f':
                strncpy(cfg->pattern_file, optarg, sizeof(cfg->pattern_file) - 1);
                break;
            case 'm':
                cfg->num_mpi_ranks = (uint32_t)strtoul(optarg, NULL, 10);
                break;
            case 't':
                cfg->num_omp_threads = (uint32_t)strtoul(optarg, NULL, 10);
                break;
            case 's': {
                char *chunk = strchr(optarg, ',');
                if (chunk) {
                    *chunk = '\0';
                    cfg->schedule_chunk = (uint32_t)strtoul(chunk + 1, NULL, 10);
                }
                strncpy(cfg->schedule_type, optarg, sizeof(cfg->schedule_type) - 1);
                break;
            }
            case 'r':
                cfg->random_seed = (uint32_t)strtoul(optarg, NULL, 10);
                break;
            case 'o':
                strncpy(cfg->output_file, optarg, sizeof(cfg->output_file) - 1);
                break;
            case 'x':
                strncpy(cfg->output_format, optarg, sizeof(cfg->output_format) - 1);
                break;
            case 'n':
                cfg->num_repetitions = (uint32_t)strtoul(optarg, NULL, 10);
                break;
            case 'v':
                cfg->verbose = 1;
                break;
            case 'h':
            default:
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }
}

int validate_config(const Config *cfg) {
    // 1. Packet count validation
    if (cfg->packet_count == 0) {
        fprintf(stderr, "Configuration Error: packet_count must be greater than 0.\n");
        return 0;
    }

    // 2. OpenMP Thread validation against available processors
    long physical_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (physical_cores > 0 && cfg->num_omp_threads > (uint32_t)physical_cores) {
        fprintf(stderr, "Configuration Error: Target threads (%u) exceeds physical available cores (%ld).\n",
                cfg->num_omp_threads, physical_cores);
        return 0;
    }

    // 3. Dataset file existence check
    if (access(cfg->dataset_file, F_OK) != 0) {
        fprintf(stderr, "Configuration Error: Dataset file '%s' does not exist.\n", cfg->dataset_file);
        return 0;
    }

    // 4. Pattern file existence check
    if (access(cfg->pattern_file, F_OK) != 0) {
        fprintf(stderr, "Configuration Error: Pattern file '%s' does not exist.\n", cfg->pattern_file);
        return 0;
    }

    // 5. Output format validation
    if (strcmp(cfg->output_format, "csv") != 0 && strcmp(cfg->output_format, "json") != 0) {
        fprintf(stderr, "Configuration Error: Output format must be either 'csv' or 'json'.\n");
        return 0;
    }

    // 6. RAM threshold warning
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        uint64_t total_ram = (uint64_t)info.totalram * info.mem_unit;
        if (cfg->dataset_size > total_ram) {
            fprintf(stderr, "Configuration Warning: Target dataset size (%lu MB) exceeds total system memory (%lu MB).\n",
                    cfg->dataset_size / (1024 * 1024), total_ram / (1024 * 1024));
        }
    }

    return 1;
}

void print_config(const Config *cfg) {
    printf("=== Configuration Parameters ===\n");
    printf("  Dataset Size:       %lu Bytes (%lu MB)\n", cfg->dataset_size, cfg->dataset_size / (1024 * 1024));
    printf("  Packet Count:       %u\n", cfg->packet_count);
    printf("  Dataset File:       %s\n", cfg->dataset_file);
    printf("  Pattern File:       %s\n", cfg->pattern_file);
    printf("  MPI Ranks:          %u\n", cfg->num_mpi_ranks);
    printf("  OpenMP Threads:     %u\n", cfg->num_omp_threads);
    printf("  OMP Schedule:       %s (Chunk: %u)\n", cfg->schedule_type, cfg->schedule_chunk);
    printf("  Output File:        %s\n", cfg->output_file);
    printf("  Output Format:      %s\n", cfg->output_format);
    printf("  Repetitions:        %u\n", cfg->num_repetitions);
    printf("  Random Seed:        %u\n", cfg->random_seed);
    printf("  Verbose Logging:    %s\n", cfg->verbose ? "ENABLED" : "DISABLED");
    printf("=================================\n");
}

void capture_metadata(SystemMetadata *meta) {
    memset(meta, 0, sizeof(SystemMetadata));

    // Hostname
    if (gethostname(meta->hostname, sizeof(meta->hostname) - 1) != 0) {
        strncpy(meta->hostname, "unknown", sizeof(meta->hostname) - 1);
    }

    // GCC version
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#ifdef __GNUC__
    snprintf(meta->gcc_version, sizeof(meta->gcc_version), "%d.%d.%d", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
    strncpy(meta->gcc_version, "Non-GCC Compiler", sizeof(meta->gcc_version) - 1);
#endif

    // OpenMPI Version Check
#if defined(OMPI_MAJOR_VERSION) && defined(OMPI_MINOR_VERSION) && defined(OMPI_RELEASE_VERSION)
    snprintf(meta->mpi_version, sizeof(meta->mpi_version), "OpenMPI %d.%d.%d", 
             OMPI_MAJOR_VERSION, OMPI_MINOR_VERSION, OMPI_RELEASE_VERSION);
#elif defined(MPI_VERSION)
    snprintf(meta->mpi_version, sizeof(meta->mpi_version), "MPI Standard v%d.%d", MPI_VERSION, MPI_SUBVERSION);
#else
    strncpy(meta->mpi_version, "No MPI library compiled", sizeof(meta->mpi_version) - 1);
#endif

    // System architecture (uname)
    struct utsname sys_u;
    if (uname(&sys_u) == 0) {
        snprintf(meta->sys_info, sizeof(meta->sys_info), "%s %s %s %s", 
                 sys_u.sysname, sys_u.release, sys_u.version, sys_u.machine);
    } else {
        strncpy(meta->sys_info, "unknown_sysinfo", sizeof(meta->sys_info) - 1);
    }
}