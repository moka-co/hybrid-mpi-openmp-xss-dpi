/* test_ac.c – compile with: gcc -o test_ac test_ac.c aho_corasick.c */
#include <stdio.h>
#include <stdint.h>
#include "../src/pattern_matching.h"  /* relative path to src/ */

int main(void)
{
    const char *patterns[] = {
        "<script>",
        "onerror=",
        "javascript:",
        "eval(",
        "onclick="
    };
    int np = 5;

    ACAutomaton *ac = ac_build(patterns, np);

    const char *packet = "GET /page?x=<script>alert(1)</script>&y=onerror=1 HTTP/1.1";
    printf("Scanning: %s\n\n", packet);

    ACMatchList ml = ac_scan(ac, (const uint8_t *)packet, strlen(packet));

    for (int i = 0; i < ml.count; i++) {
        printf("  Match: pattern_id=%d (\"%s\") at offset %d\n",
               ml.matches[i].pattern_id,
               patterns[ml.matches[i].pattern_id],
               ml.matches[i].offset);
    }

    ac_free_matches(&ml);
    ac_free(ac);
    return 0;
}