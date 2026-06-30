#include "config.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    Config cfg;
    SystemMetadata meta;

    // 1. Initialize with defaults
    init_default_config(&cfg);

    // 2. Parse command-line flags
    parse_arguments(argc, argv, &cfg);

    // 3. Validate settings against system boundaries
    if (!validate_config(&cfg)) {
        fprintf(stderr, "Initialization failed due to invalid configuration settings.\n");
        exit(EXIT_FAILURE);
    }

    // 4. Capture build and environment metadata
    capture_metadata(&meta);

    // 5. Output configurations (Reproducibility baseline)
    print_config(&cfg);
    
    printf("\n=== Metadata Environment Verification ===\n");
    printf("Host: %s | GCC: %s\n", meta.hostname, meta.gcc_version);
    printf("OS:   %s\n", meta.sys_info);
    
    printf("\nConfig pipeline fully operational. Handing off to engine...\n");
    return 0;
}