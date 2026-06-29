/* test_ac_file.c – Scans a custom list of test packets */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../src/pattern_matching.h"

const char *FILENAME = "datasets-private/string_xss_only.txt";

int main(void)
{
    // 1. Load your patterns from the file as before
    FILE *fp = fopen(FILENAME, "r");
    if (!fp) {
        perror("Error opening pattern file");
        return 1;
    }

    int max_patterns = 2048;
    char **patterns = malloc(max_patterns * sizeof(char *));
    int pattern_count = 0;
    char line[4096];

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) == 0) continue;
        if (pattern_count >= max_patterns) {
            max_patterns *= 2;
            patterns = realloc(patterns, max_patterns * sizeof(char *));
        }
        patterns[pattern_count++] = strdup(line);
    }
    fclose(fp);

    printf("Loaded %d patterns. Building automaton...\n", pattern_count);
    ACAutomaton *ac = ac_build((const char **)patterns, pattern_count);

    if (ac) {
        // Custom test packets
        const char *test_packets[] = {
            "/index.php?id=<script>alert(1)</script>",
            "POST /login.php HTTP/1.1\r\n\r\nuser=admin&pass=1' OR '1'='1",
            "/search?q=javascript:alert(42873)",
            "Normal traffic here with no malicious content"
        };
        int num_test_packets = 4;

        printf("Automaton ready. Scanning custom packets...\n\n");

        // Scan each packet
        for (int i = 0; i < num_test_packets; i++) {
            const char *packet = test_packets[i];
            printf("--- Packet [%d] ---\n%s\n", i, packet);
            
            ACMatchList ml = ac_scan(ac, (const uint8_t *)packet, strlen(packet));

            if (ml.count == 0) {
                printf("Result: No matches found.\n");
            } else {
                for (int j = 0; j < ml.count; j++) {
                    printf("  Match: pattern \"%s\" at offset %d\n",
                           patterns[ml.matches[j].pattern_id],
                           ml.matches[j].offset);
                }
            }
            ac_free_matches(&ml);
            printf("\n");
        }
        ac_free(ac);
    }

    // Cleanup resources
    for (int i = 0; i < pattern_count; i++) free(patterns[i]);
    free(patterns);

    return 0;
}