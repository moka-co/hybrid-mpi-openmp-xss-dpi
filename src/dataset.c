// src/dataset.c
//
// See dataset.h for the public API contract.

#include "dataset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define DATASET_MAGIC 0x54534544u // "DSET" read as a little-endian uint32

// -----------------------------------------------------------------------
// RNG helpers
// -----------------------------------------------------------------------

// Uniform double in [0, 1).
static double next_uniform(void)
{
    return (double)rand() / ((double)RAND_MAX + 1.0);
}

// Standard normal sample via the Box-Muller transform.
static double sample_gaussian(void)
{
    double u1 = next_uniform();
    double u2 = next_uniform();

    // Guard against log(0) if u1 happens to land on exactly 0.
    if (u1 < 1e-12) u1 = 1e-12;

    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

// Sample a packet length from the lognormal distribution, clamped to
// [min_len, max_len].
static size_t sample_length(const LengthDistParams *p)
{
    double z   = sample_gaussian();
    double raw = exp(p->mu + p->sigma * z);

    size_t len = (size_t)raw;
    if (len < p->min_len) len = p->min_len;
    if (len > p->max_len) len = p->max_len;
    return len;
}

// Fill a buffer with random printable ASCII bytes (space through '~').
static void fill_random_ascii(uint8_t *buf, size_t len)
{
    const int lo = 0x20; // ' '
    const int hi = 0x7E; // '~'
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(lo + (rand() % (hi - lo + 1)));
    }
}

// -----------------------------------------------------------------------
// Pattern lookup by length
// -----------------------------------------------------------------------

// Helper structure for sorting pattern indices by length
typedef struct {
    int    index; // index into the original pattern_list
    size_t len;   // strlen(pattern_list[index]), cached
} PatternEntry;

// Comparator for PatternEntry based on length
static int cmp_pattern_entry(const void *a, const void *b)
{
    const PatternEntry *pa = (const PatternEntry *)a;
    const PatternEntry *pb = (const PatternEntry *)b;
    if (pa->len < pb->len) return -1;
    if (pa->len > pb->len) return 1;
    return 0;
}

// Builds a sorted index of patterns by their length for efficient lookups
static PatternEntry *build_sorted_pattern_index(const char **pattern_list, int num_patterns)
{
    PatternEntry *sorted = (PatternEntry *)malloc(num_patterns * sizeof(PatternEntry));
    if (!sorted) return NULL;

    for (int i = 0; i < num_patterns; i++) {
        sorted[i].index = i;
        sorted[i].len   = strlen(pattern_list[i]);
    }

    qsort(sorted, num_patterns, sizeof(PatternEntry), cmp_pattern_entry);
    return sorted;
}

// Returns the number of entries in `sorted` whose length is <= max_len
// (upper-bound binary search).
static int count_fitting_patterns(const PatternEntry *sorted, int num_patterns, size_t max_len)
{
    int lo = 0, hi = num_patterns;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (sorted[mid].len <= max_len) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

// Overwrite `len` bytes of `data` starting at a random valid offset with
// `payload`.
static void inject_payload(uint8_t *data, size_t len, const char *payload, size_t payload_len)
{
    size_t max_offset = len - payload_len;
    size_t offset = (max_offset > 0) ? ((size_t)rand() % (max_offset + 1)) : 0;
    memcpy(data + offset, payload, payload_len);
}

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

// Generates a synthetic dataset of packets
Packet *generate_packets(int count,
                          unsigned int seed,
                          LengthDistParams length_params,
                          double xss_prob,
                          const char **pattern_list,
                          int num_patterns)
{
    if (count <= 0 || num_patterns <= 0) return NULL;

    srand(seed);

    PatternEntry *sorted = build_sorted_pattern_index(pattern_list, num_patterns);
    if (!sorted) return NULL;

    Packet *packets = (Packet *)malloc(count * sizeof(Packet));
    if (!packets) {
        free(sorted);
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        size_t len = sample_length(&length_params);

        uint8_t *data = (uint8_t *)malloc(len);
        if (!data) {
            // Roll back everything allocated so far on OOM.
            for (int j = 0; j < i; j++) free(packets[j].data);
            free(packets);
            free(sorted);
            return NULL;
        }
        fill_random_ascii(data, len);

        int has_xss = 0;

        if (next_uniform() < xss_prob) {
            int fit_count = count_fitting_patterns(sorted, num_patterns, len);
            if (fit_count > 0) {
                int pick = rand() % fit_count;
                int pat_idx = sorted[pick].index;
                size_t pat_len = sorted[pick].len;

                inject_payload(data, len, pattern_list[pat_idx], pat_len);
                has_xss = 1;
            }
        }

        packets[i].data    = data;
        packets[i].len     = len;
        packets[i].has_xss = has_xss;
    }

    free(sorted);
    return packets;
}

// Frees the allocated memory for packets
void free_packets(Packet *packets, int count)
{
    if (!packets) return;
    for (int i = 0; i < count; i++) {
        free(packets[i].data);
    }
    free(packets);
}

// Saves the generated packets to a file
int save_packets_to_file(const char *filepath, const Packet *packets, int count)
{
    FILE *f = fopen(filepath, "w"); // Text mode
    if (!f) {
        perror("save_packets_to_file: fopen");
        return -1;
    }

    for (int i = 0; i < count; i++) {
        // Format: has_xss|len|data
        fprintf(f, "%d|%zu|", packets[i].has_xss, packets[i].len);
        if (fwrite(packets[i].data, 1, packets[i].len, f) != packets[i].len) {
            fclose(f);
            return -1;
        }
        fprintf(f, "\n");
    }

    fclose(f);
    return 0;
}

// Loads packets from a file
Packet *load_packets_from_file(const char *filepath, int *out_count)
{
    FILE *f = fopen(filepath, "r"); // Text mode
    if (!f) {
        perror("load_packets_from_file: fopen");
        return NULL;
    }

    // First, count lines to know how many packets to allocate
    int cnt = 0;
    char ch;
    while((ch = fgetc(f)) != EOF) {
        if(ch == '\n') cnt++;
    }
    rewind(f);

    Packet *packets = (Packet *)malloc(cnt * sizeof(Packet));
    if (!packets) {
        fclose(f);
        return NULL;
    }

    for (int i = 0; i < cnt; i++) {
        int has_xss;
        size_t len;
        
        if (fscanf(f, "%d|%zu|", &has_xss, &len) != 2) {
            for (int j = 0; j < i; j++) free(packets[j].data);
            free(packets);
            fclose(f);
            return NULL;
        }

        uint8_t *data = (uint8_t *)malloc(len);
        if (!data || fread(data, 1, len, f) != len) {
            free(data);
            for (int j = 0; j < i; j++) free(packets[j].data);
            free(packets);
            fclose(f);
            return NULL;
        }
        
        // Consume the newline
        fgetc(f); 

        packets[i].data    = data;
        packets[i].len     = len;
        packets[i].has_xss = has_xss;
    }

    fclose(f);
    *out_count = cnt;
    return packets;
}

// Loads patterns from a file (one pattern per line).
char **load_patterns_from_file(const char *filepath, int *out_count)
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

// Frees the array of patterns and the individual strings.
void free_patterns_list(char **patterns, int count)
{
    if (!patterns) return;
    for (int i = 0; i < count; i++) {
        free(patterns[i]);
    }
    free(patterns);
}
