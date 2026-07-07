/*
 * tests/generate_csic_dataset.c
 *
 * Real-data counterpart to tests/validate_dataset.c.
 *
 * validate_dataset.c cross-checks the Aho-Corasick automaton against a
 * *synthetically generated* dataset with known ground truth. This file
 * does the same cross-check, but loads real HTTP requests from the CSIC
 * 2010 dataset (already converted to a simple text format) instead of
 * generating random packets.
 *
 * INPUT FORMAT
 * ------------
 * Expects a .txt file with one request per line, produced by
 * csic2010_csv_to_txt.py using:
 *
 *     python csic2010_csv_to_txt.py csic2010.csv csic_get_post.txt \
 *         --format line --method-filter GET,POST --url-only
 *
 * Each line looks like:
 *
 *     <label>|<url_and_body>
 *
 * e.g.
 *     anomalous|http://localhost:8080/tienda1/publico/anadir.jsp?id=<script>alert(1)</script>
 *     normal|http://localhost:8080/tienda1/publico/index.jsp
 *
 * GROUND TRUTH CAVEAT
 * --------------------
 * The CSIC dataset's "anomalous" label covers ALL attack types the
 * dataset contains (SQLi, buffer overflow, path/parameter tampering,
 * etc.), not just XSS. Treating anomalous == has_xss will therefore
 * overcount false negatives: an anomalous SQLi row that (correctly)
 * contains no XSS pattern will look like a missed detection here.
 *
 * If you have refined your pattern list to be XSS-specific, use this
 * tool to get a rough signal, but do not trust false_negative counts
 * as precisely as you could with the synthetic dataset. For a tighter
 * check, pre-filter the input file to rows you've independently
 * confirmed contain an XSS payload (e.g. via a keyword grep) before
 * feeding it here, or extend the label column upstream with an
 * attack-type field if your CSV version has one.
 *
 * Compile:
 *   gcc -O3 -Wall -Isrc/ -lm -o tests/generate_csic_dataset \
 *       tests/generate_csic_dataset.c src/dataset.c src/pattern_matching.c
 *
 * Run:
 *   ./tests/generate_csic_dataset datasets-private/csic_get_post.txt \
 *       [pattern_file] [output_bin]
 *
 *   pattern_file defaults to datasets-private/string_xss_only.txt
 *   output_bin   defaults to datasets/packets_csic.bin
 */

#define _POSIX_C_SOURCE 200809L // for getline(), strdup(), ssize_t

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <ctype.h>

#include "../src/dataset.h"
#include "../src/pattern_matching.h"

#define DEFAULT_PATTERN_FILE "datasets-private/string_xss_only.txt"
#define DEFAULT_OUTPUT_BIN   "datasets/packets_csic.bin"
#define MIN_PATTERN_LEN      3 // patterns shorter than this match random text too easily


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

// -----------------------------------------------------------------------
// CSIC txt loader: "<label>|<url_and_body>" -> Packet[] with ground truth
//
// Uses getline() rather than a fixed-size fgets() buffer since request
// lines (especially with POST bodies appended) can be long.
// -----------------------------------------------------------------------

// Case-insensitive check for an exact label match ("anomalous"/"normal").
static int label_is_anomalous(const char *label)
{
    // Compare case-insensitively without modifying the input.
    const char *target = "anomalous";
    size_t i = 0;
    for (; label[i] && target[i]; i++) {
        if (tolower((unsigned char)label[i]) != target[i]) return 0;
    }
    return label[i] == '\0' && target[i] == '\0';
}

static Packet *load_packets_from_csic_txt(const char *filepath, int *out_count)
{
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        perror("load_packets_from_csic_txt: fopen");
        return NULL;
    }

    int capacity = 4096;
    Packet *packets = malloc(capacity * sizeof(Packet));
    if (!packets) {
        fclose(fp);
        return NULL;
    }

    int count = 0;
    char *line = NULL;
    size_t line_cap = 0;
    ssize_t nread;
    int skipped_malformed = 0;

    while ((nread = getline(&line, &line_cap, fp)) != -1) {
        // Strip trailing newline(s).
        while (nread > 0 && (line[nread - 1] == '\n' || line[nread - 1] == '\r')) {
            line[--nread] = '\0';
        }
        if (nread == 0) continue;

        char *sep = strchr(line, '|');
        if (!sep) {
            skipped_malformed++;
            continue;
        }

        *sep = '\0';
        const char *label = line;
        const char *content = sep + 1;
        size_t content_len = strlen(content);

        if (count >= capacity) {
            capacity *= 2;
            Packet *grown = realloc(packets, capacity * sizeof(Packet));
            if (!grown) {
                fprintf(stderr, "ERROR: Failed to grow packet array\n");
                free(line);
                for (int j = 0; j < count; j++) free(packets[j].data);
                free(packets);
                fclose(fp);
                return NULL;
            }
            packets = grown;
        }

        uint8_t *data = (uint8_t *)malloc(content_len > 0 ? content_len : 1);
        if (!data) {
            fprintf(stderr, "ERROR: Failed to allocate packet data\n");
            free(line);
            for (int j = 0; j < count; j++) free(packets[j].data);
            free(packets);
            fclose(fp);
            return NULL;
        }
        memcpy(data, content, content_len);

        packets[count].data    = data;
        packets[count].len     = content_len;
        packets[count].has_xss = label_is_anomalous(label); // see ground-truth caveat above

        count++;
    }

    free(line);
    fclose(fp);

    if (skipped_malformed > 0) {
        fprintf(stderr, "Warning: skipped %d malformed line(s) with no '|' separator\n",
                skipped_malformed);
    }

    *out_count = count;
    return packets;
}

// -----------------------------------------------------------------------
// main
// -----------------------------------------------------------------------

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <csic_txt_file> [pattern_file] [output_bin]\n", argv[0]);
        fprintf(stderr, "  csic_txt_file: output of csic2010_csv_to_txt.py "
                         "(--format line --url-only)\n");
        return 1;
    }

    const char *csic_txt_path = argv[1];
    const char *pattern_path  = (argc >= 3) ? argv[2] : DEFAULT_PATTERN_FILE;
    const char *output_bin    = (argc >= 4) ? argv[3] : DEFAULT_OUTPUT_BIN;

    FILE *log = fopen("csic_validation_log.txt", "w");
    if (!log) {
        perror("Could not open csic_validation_log.txt for writing");
        return 1;
    }

    #define LOG(...) do { printf(__VA_ARGS__); fprintf(log, __VA_ARGS__); } while (0)

    LOG("=== CSIC 2010 Real-Data Validation ===\n\n");
    LOG("Input file:   %s\n", csic_txt_path);
    LOG("Pattern file: %s\n", pattern_path);
    LOG("Output bin:   %s\n\n", output_bin);

    // --- Load patterns (same pool used for cross-checking) ---
    LOG("Loading XSS patterns...\n");
    int num_patterns = 0;
    char **patterns = load_patterns_from_file(pattern_path, &num_patterns);
    if (!patterns || num_patterns == 0) {
        LOG("ERROR: Failed to load patterns\n");
        fclose(log);
        return 1;
    }
    LOG("  Patterns loaded: %d\n\n", num_patterns);

    int before_filter = num_patterns;
    num_patterns = filter_short_patterns(patterns, num_patterns, MIN_PATTERN_LEN);
    LOG("Filtering patterns shorter than %d chars...\n", MIN_PATTERN_LEN);
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

    // --- Load the real dataset ---
    LOG("Loading CSIC dataset from %s...\n", csic_txt_path);
    int num_packets = 0;
    Packet *packets = load_packets_from_csic_txt(csic_txt_path, &num_packets);
    if (!packets || num_packets == 0) {
        LOG("ERROR: Failed to load dataset (0 packets)\n");
        ac_free(ac);
        for (int i = 0; i < num_patterns; i++) free(patterns[i]);
        free(patterns);
        fclose(log);
        return 1;
    }
    LOG("  Packets loaded: %d\n\n", num_packets);

    // --- Save to disk in the project's binary packet format, so later
    //     phases can reload the exact same real-data set without
    //     re-parsing the CSV/txt export ---
    LOG("Saving dataset to %s...\n", output_bin);
    if (save_packets_to_file(output_bin, packets, num_packets) != 0) {
        LOG("  WARNING: Failed to save dataset to disk\n\n");
    } else {
        LOG("  Done.\n\n");
    }

    // --- Check 1: length distribution ---
    LOG("--- Length Distribution ---\n");

    size_t min_len = packets[0].len, max_len = packets[0].len;
    double sum_len = 0.0;

    const size_t bucket_edges[] = {64, 128, 256, 512, 1024, 2048, 4096, 8192, SIZE_MAX};
    const int num_buckets = sizeof(bucket_edges) / sizeof(bucket_edges[0]);
    int *bucket_counts = calloc(num_buckets, sizeof(int));

    for (int i = 0; i < num_packets; i++) {
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

    double mean_len = sum_len / num_packets;

    LOG("  Min length:  %zu bytes\n", min_len);
    LOG("  Max length:  %zu bytes\n", max_len);
    LOG("  Mean length: %.1f bytes\n", mean_len);
    LOG("\n  Histogram (packet count per length bucket):\n");

    size_t prev_edge = 0;
    for (int b = 0; b < num_buckets; b++) {
        int bar_len = bucket_counts[b] / 100; // scale: 1 char per ~100 packets
        if (bar_len > 60) bar_len = 60;

        if (bucket_edges[b] == SIZE_MAX) {
            LOG("    [%5zu -   inf]: %5d  ", prev_edge, bucket_counts[b]);
        } else {
            LOG("    [%5zu - %5zu]: %5d  ", prev_edge, bucket_edges[b], bucket_counts[b]);
        }
        for (int k = 0; k < bar_len; k++) fputc('#', log);
        for (int k = 0; k < bar_len; k++) putchar('#');
        LOG("\n");
        prev_edge = bucket_edges[b] + 1;
    }
    free(bucket_counts);
    LOG("\n");

    // --- Check 2: ground-truth XSS rate in the real dataset ---
    LOG("--- Ground-Truth \"anomalous\" Rate ---\n");

    int xss_count = 0;
    for (int i = 0; i < num_packets; i++) {
        if (packets[i].has_xss) xss_count++;
    }
    double xss_pct = 100.0 * xss_count / num_packets;

    LOG("  Packets labeled anomalous: %d / %d (%.2f%%)\n", xss_count, num_packets, xss_pct);
    LOG("  NOTE: \"anomalous\" here means \"CSIC labeled this row as an attack of\n");
    LOG("  some kind\", not specifically \"contains XSS\". See caveat at top of\n");
    LOG("  this file before trusting false_negative numbers below.\n\n");

    // --- Check 3: cross-check automaton detection against ground truth ---
    LOG("--- Automaton Detection vs. Ground Truth ---\n");

    int true_positive  = 0; // labeled anomalous, automaton found >=1 match
    int false_negative = 0; // labeled anomalous, automaton found 0 matches
    int true_negative  = 0; // labeled normal,    automaton found 0 matches
    int false_positive = 0; // labeled normal,    automaton found >=1 match

    for (int i = 0; i < num_packets; i++) {
        ACMatchList ml = ac_scan(ac, packets[i].data, packets[i].len);
        int detected = (ml.count > 0);

        if (packets[i].has_xss && detected)        true_positive++;
        else if (packets[i].has_xss && !detected)   false_negative++;
        else if (!packets[i].has_xss && !detected)  true_negative++;
        else                                        false_positive++;

        ac_free_matches(&ml);
    }

    LOG("  True positives  (anomalous, detected):   %d\n", true_positive);
    LOG("  False negatives (anomalous, missed):     %d\n", false_negative);
    LOG("  True negatives  (normal, clean):         %d\n", true_negative);
    LOG("  False positives (normal, detected):      %d\n", false_positive);

    if (true_positive + false_negative > 0) {
        double recall = 100.0 * true_positive / (true_positive + false_negative);
        LOG("\n  Recall on anomalous rows: %.2f%%\n", recall);
        LOG("  (Expect this to be well under 100%% -- most anomalous rows are\n");
        LOG("  non-XSS attacks your XSS pattern list was never meant to catch.)\n");
    }
    if (false_positive + true_negative > 0) {
        double fpr = 100.0 * false_positive / (false_positive + true_negative);
        LOG("\n  False positive rate on normal rows: %.2f%%\n", fpr);
        LOG("  (This number IS meaningful regardless of the caveat above --\n");
        LOG("  it tells you how often your patterns fire on legitimate traffic.)\n");
    }

    LOG("\n=== Validation Complete ===\n");

    #undef LOG

    // Cleanup
    free_packets(packets, num_packets);
    ac_free(ac);
    for (int i = 0; i < num_patterns; i++) free(patterns[i]);
    free(patterns);
    fclose(log);

    return 0;
}