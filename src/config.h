#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

typedef struct {
    uint64_t dataset_size;        // Total bytes
    uint32_t packet_count;
    char dataset_file[256];       // Path to binary packet dataset (see dataset.h)
    char pattern_file[256];
    uint32_t num_patterns;
    uint32_t num_mpi_ranks;
    uint32_t num_omp_threads;
    char schedule_type[16];       // "static" or "dynamic" or "guided"
    uint32_t schedule_chunk;
    char output_file[256];
    char output_format[16];       // "csv" or "json"
    uint32_t num_repetitions;
    uint32_t random_seed;
    int verbose;
} Config;

// Metadata structure for capturing build and runtime environment
typedef struct {
    char hostname[256];
    char mpi_version[256];
    char gcc_version[256];
    char sys_info[512];
} SystemMetadata;

// Function declarations
void init_default_config(Config *cfg);
void parse_arguments(int argc, char *argv[], Config *cfg);
int validate_config(const Config *cfg);
void print_config(const Config *cfg);
void capture_metadata(SystemMetadata *meta);
void print_usage(const char *prog_name);

#endif // CONFIG_H