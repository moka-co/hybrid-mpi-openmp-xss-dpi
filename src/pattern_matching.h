#ifndef PATTERN_MATCHING_H
#define PATTERN_MATCHING_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define AC_ALPHABET_SIZE 256   /* one entry per possible byte value */
#define AC_MAX_STATES   8156  /* max nodes in the trie; */

// Aho-Corasick automaton / Trie implementation

/**
 * Represents a single node in the Aho-Corasick automaton.
 */
typedef struct {
    int goto_table[AC_ALPHABET_SIZE]; // next state for each possible input byte
    int failure; // fallback state when no transition exists
    int output;  // index into a match-output list (-1 = no match at this state)
} ACState;

/**
 * The automaton itself.
 *
 * Thread-safety:
 * The structure itself (states, output_next, num_states, etc.) is read-only 
 * once built via ac_build(). Multiple threads can safely scan concurrently 
 * using the same ACAutomaton instance.
 */
typedef struct {
    ACState *states;      // Flat array of states
    int *output_next;     // Chains pattern outputs at the same state
    int num_states;       // Current state count
    int capacity;         // Allocated capacity
    char **patterns;      // Original pattern strings
    int num_patterns;     // Number of patterns
} ACAutomaton;

/**
 * Represents a single match, containing the pattern ID and the offset in the packet.
 */
typedef struct {
    int pattern_id;
    int offset;
} ACMatch;

/**
 * List of matches for a scan operation.
 * Must be freed with ac_free_matches() when done.
 */
typedef struct {
    ACMatch *matches;
    int      count;
    int      capacity;
} ACMatchList;

/* --- Public API --- */

/**
 * Builds the Aho-Corasick automaton from an array of pattern strings.
 * Returns a heap-allocated ACAutomaton*. Must be freed with ac_free().
 */
ACAutomaton *ac_build(const char **patterns, int num_patterns);

/**
 * Scans a single packet (data[0..len-1]) against the automaton.
 * Thread-safe: Each thread must manage its own ACMatchList.
 * Returns an ACMatchList; caller must call ac_free_matches() when done.
 */
ACMatchList ac_scan(const ACAutomaton *ac, const uint8_t *data, size_t len);

/**
 * Frees an ACMatchList returned by ac_scan().
 */
void ac_free_matches(ACMatchList *ml);

/**
 * Frees an ACAutomaton returned by ac_build().
 */
void ac_free(ACAutomaton *ac);

/**
 * Initializes a match list with a starting capacity.
 * Called once per thread before the scanning loop.
 */
void ac_matchlist_init(ACMatchList *ml, int initial_capacity);

/**
 * Scans a packet, writing matches into a pre-allocated ACMatchList.
 * Resets ml->count to 0 internally. Thread-safe if each thread uses a 
 * distinct pre-allocated ACMatchList.
 */
void ac_scan_into(const ACAutomaton *ac, const uint8_t *data, size_t len, ACMatchList *ml);

#endif // PATTERN_MATCHING_H