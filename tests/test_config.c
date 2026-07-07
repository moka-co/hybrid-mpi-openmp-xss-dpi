#include "../src/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

void test_default_initialization() {
    Config cfg;
    init_default_config(&cfg);
    
    assert(cfg.packet_count == 1000000);
    assert(cfg.num_omp_threads == 0);
    assert(cfg.random_seed == 42);
    assert(strcmp(cfg.schedule_type, "static") == 0);
    assert(strcmp(cfg.output_format, "csv") == 0);
    printf("[PASS] Default initialization evaluation verified.\n");
}

void test_validation_rules() {
    Config cfg;
    init_default_config(&cfg);

    // Create a temporary file to mock pattern file existence
    char tmp_pattern[] = "test_patterns_mock.txt";
    FILE *f = fopen(tmp_pattern, "w");
    assert(f != NULL);
    fprintf(f, "test_pattern\n");
    fclose(f);
    
    strncpy(cfg.pattern_file, tmp_pattern, sizeof(cfg.pattern_file) - 1);

    // Valid configuration tracking
    assert(validate_config(&cfg) == 1);

    // Invalid packet count validation
    cfg.packet_count = 0;
    assert(validate_config(&cfg) == 0);
    cfg.packet_count = 100;

    // Invalid OMP thread assignment tracking
    cfg.num_omp_threads = 99999; 
    assert(validate_config(&cfg) == 0);
    cfg.num_omp_threads = 1;

    // Invalid pattern file tracking
    strncpy(cfg.pattern_file, "non_existent_file_xyz.txt", sizeof(cfg.pattern_file) - 1);
    assert(validate_config(&cfg) == 0);

    unlink(tmp_pattern); // Cleanup dummy file
    printf("[PASS] Configuration metric boundaries validation checked.\n");
}

void test_metadata_capture() {
    SystemMetadata meta;
    capture_metadata(&meta);

    assert(strlen(meta.hostname) > 0);
    assert(strlen(meta.gcc_version) > 0);
    assert(strlen(meta.sys_info) > 0);
    
    printf("[PASS] Structural metadata capture validation verified.\n");
    printf("       Captured Host: %s\n", meta.hostname);
    printf("       Captured GCC:  %s\n", meta.gcc_version);
    printf("       Captured OS:   %s\n", meta.sys_info);
}

int main() {
    printf("Running Configuration Suite Unit Tests...\n\n");
    test_default_initialization();
    test_validation_rules();
    test_metadata_capture();
    printf("\nAll suite tests successfully cleared.\n");
    return 0;
}