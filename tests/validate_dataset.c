// tests/validate_dataset.c
// Compile: gcc -O3 -Wall -Isrc/ -lm -o tests/validate_dataset 
//            tests/validate_dataset.c src/dataset.c src/pattern_matching.c
// Run: ./tests/validate_dataset
//
// Generates 10,000 synthetic packets and checks:
//   1. Length distribution looks lognormal (mean, min/max, histogram)
//   2. ~5% of packets are flagged has_xss
//   3. The Aho-Corasick automaton's detections agree with the ground-truth
//      has_xss labels (true positives / false negatives / false positives)
//
// Writes a report to validation_log.txt.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdint.h>
#include <limits.h>
#include "../src/dataset.h"
#include "../src/pattern_matching.h"

#define NUM_PACKETS   1000000 //1.000.000
#define SEED          42
#define XSS_PROB      0.05
#define MIN_PATTERN_LEN  3 // pattern shorter than this match random text

// Load patterns from a file (one pattern per line), growing dynamically.
static char **load_patterns_from_file(const char *filepath, int *out_count)
{
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        perror("Error opening pattern file");
        return NULL;
    }

    int capacity = 256;
    char **patterns = malloc(capacity * sizeof(char *));
    if (!patterns) {
        fclose(fp);
        return NULL;
    }

    int count = 0;
    char line[4096];

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) == 0) continue;

        if (count >= capacity) {
            capacity *= 2;
            char **grown = realloc(patterns, capacity * sizeof(char *));
            if (!grown) {
                fprintf(stderr, "ERROR: Failed to grow pattern array\n");
                fclose(fp);
                return NULL;
            }
            patterns = grown;
        }

        patterns[count] = strdup(line);
        if (!patterns[count]) {
            fprintf(stderr, "ERROR: Failed to allocate memory for pattern\n");
            fclose(fp);
            return NULL;
        }
        count++;
    }

    fclose(fp);
    *out_count = count;
    return patterns;
}

// Drop patterns shorter than min_len in place, compacting the array.
// Returns the new count. Freed strings are released; the array itself is
// NOT reallocated (caller's original capacity remains, just unused tail).
static int filter_short_patterns(char **patterns, int count, int min_len)
{
    int kept = 0;
    for (int i = 0; i < count; i++) {
        if ((int)strlen(patterns[i]) >= min_len) {
            patterns[kept++] = patterns[i];
        } else {
            free(patterns[i]);
        }
    }
    return kept;
}

// Print a quick length histogram for the pattern list, so we can see
// *why* the automaton might be over-matching (lots of very short patterns
// will match random background text by sheer chance).
static void log_pattern_length_stats(FILE *log, char **patterns, int count)
{
    int min_len = INT_MAX, max_len = 0;
    long sum_len = 0;
    int under4 = 0, under8 = 0;
 
    for (int i = 0; i < count; i++) {
        int len = (int)strlen(patterns[i]);
        if (len < min_len) min_len = len;
        if (len > max_len) max_len = len;
        sum_len += len;
        if (len < 4) under4++;
        if (len < 8) under8++;
    }
 
    #define PLOG(...) do { printf(__VA_ARGS__); fprintf(log, __VA_ARGS__); } while (0)
    PLOG("  Pattern length stats: min=%d max=%d mean=%.1f\n",
         min_len, max_len, (double)sum_len / count);
    PLOG("  Patterns shorter than 4 chars: %d (%.2f%%)\n",
         under4, 100.0 * under4 / count);
    PLOG("  Patterns shorter than 8 chars: %d (%.2f%%)\n",
         under8, 100.0 * under8 / count);
    #undef PLOG
}

int main(void)
{
    FILE *log = fopen("validation_log.txt", "w");
    if (!log) {
        perror("Could not open validation_log.txt for writing");
        return 1;
    }

    // Small helper macro: print to both stdout and the log file at once.
    #define LOG(...) do { printf(__VA_ARGS__); fprintf(log, __VA_ARGS__); } while (0)

    LOG("=== Synthetic Dataset Validation ===\n\n");

    // --- Load XSS patterns (same pool used both for injection and for the
    //     automaton, so we have a meaningful ground truth to check against) ---
    LOG("Loading XSS patterns...\n");
    int num_patterns = 0;
    char **patterns = load_patterns_from_file("datasets-private/string_xss_only.txt", &num_patterns);
    if (!patterns || num_patterns == 0) {
        LOG("ERROR: Failed to load patterns\n");
        fclose(log);
        return 1;
    }

    LOG("  Patterns loaded: %d\n\n", num_patterns);

    LOG("--- Pattern Length Diagnostic ---\n");
    log_pattern_length_stats(log, patterns, num_patterns);
    LOG("\n");
 
    int before_filter = num_patterns;
    num_patterns = filter_short_patterns(patterns, num_patterns, MIN_PATTERN_LEN);
    LOG("Filtering patterns shorter than %d chars (these match random\n", MIN_PATTERN_LEN);
    LOG("background text by chance too often to be useful signatures)...\n");
    LOG("  Removed: %d, Remaining: %d\n\n", before_filter - num_patterns, num_patterns);

    // --- Build the automaton ---
    LOG("Building Aho-Corasick automaton...\n");
    ACAutomaton *ac = ac_build((const char **)patterns, num_patterns);
    if (!ac) {
        LOG("ERROR: Failed to build automaton\n");
        fclose(log);
        return 1;
    }
    LOG("  Automaton states: %d\n\n", ac->num_states);

    // --- Generate the synthetic dataset ---
    LOG("Generating %d synthetic packets (seed=%d, xss_prob=%.2f)...\n",
        NUM_PACKETS, SEED, XSS_PROB);

    LengthDistParams length_params = {
        .mu      = 6.0,
        .sigma   = 1.5,
        .min_len = 64,
        .max_len = 8192
    };

    Packet *packets = generate_packets(NUM_PACKETS, SEED, length_params,
                                        XSS_PROB, (const char **)patterns, num_patterns);
    if (!packets) {
        LOG("ERROR: Failed to generate packets\n");
        fclose(log);
        return 1;
    }
    LOG("  Done.\n\n");

    // --- Save to disk so later phases (and re-validation) can reload the
    //     exact same dataset without regenerating it ---
    LOG("Saving dataset to packets.bin...\n");
    if (save_packets_to_file("datasets/packets.bin", packets, NUM_PACKETS) != 0) {
        LOG("  WARNING: Failed to save dataset to disk\n\n");
    } else {
        LOG("  Done.\n\n");
    }

    // --- Check 1: length distribution ---
    LOG("--- Length Distribution ---\n");

    size_t min_len = packets[0].len, max_len = packets[0].len;
    double sum_len = 0.0;

    // Histogram buckets, doubling edges from 64B up to 8192B.
    const size_t bucket_edges[] = {64, 128, 256, 512, 1024, 2048, 4096, 8192};
    const int num_buckets = sizeof(bucket_edges) / sizeof(bucket_edges[0]);
    int bucket_counts[8] = {0};

    for (int i = 0; i < NUM_PACKETS; i++) {
        size_t len = packets[i].len;
        sum_len += (double)len;
        if (len < min_len) min_len = len;
        if (len > max_len) max_len = len;

        for (int b = 0; b < num_buckets; b++) {
            if (len <= bucket_edges[b]) {
                bucket_counts[b]++;
                break;
            }
        }
    }

    double mean_len = sum_len / NUM_PACKETS;

    LOG("  Min length:  %zu bytes\n", min_len);
    LOG("  Max length:  %zu bytes\n", max_len);
    LOG("  Mean length: %.1f bytes\n", mean_len);
    LOG("\n  Histogram (packet count per length bucket):\n");

    size_t prev_edge = 0;
    for (int b = 0; b < num_buckets; b++) {
        int bar_len = bucket_counts[b] / 100; // scale: 1 char per ~100 packets
        if (bar_len > 60) bar_len = 60;       // cap bar width for readability

        LOG("    [%5zu - %5zu]: %5d  ", prev_edge, bucket_edges[b], bucket_counts[b]);
        for (int k = 0; k < bar_len; k++) fputc('#', log);
        for (int k = 0; k < bar_len; k++) putchar('#');
        LOG("\n");
        prev_edge = bucket_edges[b] + 1;
    }

    LOG("\n  Expected shape: heavy concentration in small buckets (64-1024B),\n");
    LOG("  thinning tail toward 8192B, with a cluster AT 8192B from clamping.\n\n");

    // --- Check 2: XSS injection rate ---
    LOG("--- XSS Injection Rate (ground truth) ---\n");

    int xss_count = 0;
    for (int i = 0; i < NUM_PACKETS; i++) {
        if (packets[i].has_xss) xss_count++;
    }
    double xss_pct = 100.0 * xss_count / NUM_PACKETS;

    LOG("  Packets with has_xss=1: %d / %d (%.2f%%)\n", xss_count, NUM_PACKETS, xss_pct);
    LOG("  Target probability:     %.2f%%\n", XSS_PROB * 100.0);
    LOG("  (Some packets flagged for injection may end up clean if no pattern\n");
    LOG("   fits the sampled length; small deviation from the target is expected.)\n\n");

    // --- Check 3: cross-check automaton detection against ground truth ---
    LOG("--- Automaton Detection vs. Ground Truth ---\n");

    int true_positive  = 0; // has_xss=1, automaton found >=1 match
    int false_negative = 0; // has_xss=1, automaton found 0 matches
    int true_negative  = 0; // has_xss=0, automaton found 0 matches
    int false_positive = 0; // has_xss=0, automaton found >=1 match (collision)

    for (int i = 0; i < NUM_PACKETS; i++) {
        ACMatchList ml = ac_scan(ac, packets[i].data, packets[i].len);
        int detected = (ml.count > 0); //TODO: this is temporary, 
        // the problem with pattern matching is that it overestimates false positives

        if (packets[i].has_xss && detected)        true_positive++;
        else if (packets[i].has_xss && !detected)   false_negative++;
        else if (!packets[i].has_xss && !detected)  true_negative++;
        else                                        false_positive++;

        ac_free_matches(&ml);
    }

    LOG("  True positives  (xss, detected):     %d\n", true_positive);
    LOG("  False negatives (xss, missed):       %d\n", false_negative);
    LOG("  True negatives  (clean, clean):      %d\n", true_negative);
    LOG("  False positives (clean, detected):   %d\n", false_positive);
    LOG("\n  Expected: false_negative == 0 (every injected payload should be\n");
    LOG("  found, since it's an exact substring drawn from the pattern list).\n");
    LOG("  false_positive > 0 is possible but rare (random background bytes\n");
    LOG("  coincidentally containing a short pattern), and should be small.\n\n");

    if (false_negative == 0) {
        LOG("  PASS: all injected payloads were detected.\n\n");
    } else {
        LOG("  FAIL: %d injected payloads were NOT detected -- check injection\n", false_negative);
        LOG("  or scanning logic.\n\n");
    }

    LOG("=== Validation Complete ===\n");

    #undef LOG

    // Cleanup
    free_packets(packets, NUM_PACKETS);
    ac_free(ac);
    for (int i = 0; i < num_patterns; i++) free(patterns[i]);
    free(patterns);
    fclose(log);

    return 0;
}