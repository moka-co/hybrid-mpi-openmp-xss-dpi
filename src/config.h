#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// Configuration parameters for the DPI engine
typedef struct {
    uint64_t dataset_size;        // Total dataset size in bytes
    uint32_t packet_count;        // Number of packets in dataset
    char dataset_file[256];       // Path to binary packet dataset (see dataset.h)
    char pattern_file[256];       // Path to signature pattern file
    uint32_t num_patterns;        // Number of patterns loaded
    uint32_t num_mpi_ranks;       // Number of MPI ranks (if MPI enabled)
    uint32_t num_omp_threads;     // Number of OpenMP threads (if OpenMP enabled)
    char strategy_type[16];       // Execution strategy: "sequential", "omp", "mpi", "hybrid"
    char schedule_type[16];       // OpenMP schedule type: "static", "dynamic", "guided"
    uint32_t schedule_chunk;      // OpenMP schedule chunk size
    char output_file[256];        // Path to output results file
    char output_format[16];       // Output format: "csv" or "json"
    uint32_t num_repetitions;     // Number of execution iterations
    uint32_t random_seed;         // Random seed for reproducibility
    int verbose;                  // Enable/disable verbose logging
} Config;

// Metadata structure for capturing build and runtime environment
typedef struct {
    char hostname[256];           // System hostname
    char mpi_version[256];        // MPI library version
    char gcc_version[256];        // GCC compiler version
    char sys_info[512];           // System architecture information
} SystemMetadata;

// Initializes the configuration with default values
void init_default_config(Config *cfg);
// Parses command-line arguments to override defaults
void parse_arguments(int argc, char *argv[], Config *cfg);
// Validates the configuration parameters
int validate_config(const Config *cfg);
// Prints the current configuration settings
void print_config(const Config *cfg);
// Captures system-specific metadata
void capture_metadata(SystemMetadata *meta);
// Displays usage instructions
void print_usage(const char *prog_name);

#endif // CONFIG_H
