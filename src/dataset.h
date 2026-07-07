// src/dataset.h
//
// Synthetic packet dataset generator.
//
// Packets are variable-length byte strings meant to stand in for network
// payloads. Lengths follow a lognormal distribution (mimicking the
// small-packet-heavy, long-tailed shape of real traffic), and a configurable
// fraction of packets have an XSS payload embedded at a random offset.

#ifndef DATASET_H
#define DATASET_H

#include <stdint.h>
#include <stddef.h>

// A single synthetic packet.
// data    : raw bytes (owned by the packet, free with free_packets())
// len     : number of bytes in data
// has_xss : 1 if a pattern was injected into this packet, 0 otherwise
//           (ground truth label, independent of any automaton's detection)
typedef struct {
    uint8_t *data;
    size_t   len;
    int      has_xss;
} Packet;

// Parameters controlling the packet length distribution.
// mu, sigma : lognormal shape parameters (length = exp(mu + sigma * z),
//             z ~ standard normal)
// min_len   : hard floor applied after sampling
// max_len   : hard ceiling applied after sampling
typedef struct {
    double mu;
    double sigma;
    size_t min_len;
    size_t max_len;
} LengthDistParams;

// Generate `count` synthetic packets.
//
// seed          : seeds the internal RNG (srand) so runs are reproducible
// length_params : lognormal distribution parameters (see above)
// xss_prob      : per-packet probability [0,1] of injecting a pattern
// pattern_list  : pool of candidate XSS payload strings to inject from
// num_patterns  : number of entries in pattern_list
//
// For each packet flagged for injection, only patterns that fit inside the
// sampled packet length are eligible; if none fit (very short packet, e.g.
// near the 64B floor, paired with an unusually long payload), the packet is
// left clean and has_xss is 0. This is expected and noted in validation.
//
// Returns a heap-allocated array of `count` packets, or NULL on failure.
// Caller must free with free_packets().
Packet *generate_packets(int count,
                          unsigned int seed,
                          LengthDistParams length_params,
                          double xss_prob,
                          const char **pattern_list,
                          int num_patterns);

// Free an array of packets returned by generate_packets() or
// load_packets_from_file().
void free_packets(Packet *packets, int count);

// Serialize packets to a binary file so later phases (validation, the
// MPI/OpenMP scanning engine) can reload the exact same dataset without
// regenerating it.
//
// File format (little-endian, no padding):
//   uint32_t magic       ("DSET" as 4 bytes, see DATASET_MAGIC)
//   uint32_t count
//   repeated `count` times:
//     uint32_t len
//     uint8_t  has_xss
//     uint8_t  data[len]
//
// Returns 0 on success, -1 on failure.
int save_packets_to_file(const char *filepath, const Packet *packets, int count);

// Load packets previously written by save_packets_to_file().
// Returns a heap-allocated array and sets *out_count, or NULL on failure.
// Caller must free with free_packets().s
Packet *load_packets_from_file(const char *filepath, int *out_count);

// Loads patterns from a file (one pattern per line).
// Returns a heap-allocated array of strings, or NULL on failure.
char **load_patterns_from_file(const char *filepath, int *out_count);

// Frees the array of patterns and the individual strings.
void free_patterns_list(char **patterns, int count);

#endif // DATASET_H