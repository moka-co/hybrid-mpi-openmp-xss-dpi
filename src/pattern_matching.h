#ifndef PATTERN_MATCHING_H
#define PATTERN_MATCHING_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define AC_ALPHABET_SIZE 256   /* one entry per possible byte value */
#define AC_MAX_STATES   8156  /* max nodes in the trie; */

// Aho-Corasick automaton / Trie implementation

// Represents a single node in the automaton
typedef struct {
    int goto_table[AC_ALPHABET_SIZE]; //next state for each possible input byte (or -1 if none)
    int failure; // fallback state when no transition exists
    int output;  //index into a match-output list (-1 = no match at this state)
} ACState;

//Note: a linked list of outputs should be used to handle overlapping patterns


/*
 * The automaton itself.
 *
 * states      : flat array of ACState nodes
 * num_states  : how many nodes are currently allocated
 * patterns    : the original pattern strings (owned by the automaton)
 * num_patterns: number of patterns
 * output_next : parallel array; output_next[i] chains to the next
 *               pattern output at the same state (-1 = end of chain)
 *               (handles the case where multiple patterns end at one state)
 */

//ACAutomaton represents the automaton itself, it is in fact composed of ACState(s) defined above.
typedef struct {
    ACState *states;      // Heap-allocated array
    int *output_next;     // chains to the next pattern output at the same time
    int num_states;       // Current count
    int capacity;         // Current allocated capacity
    char **patterns;    //pointer to the original pattern strings (owned by the automaton)
    int num_patterns; //number of patterns
} ACAutomaton;

//ACMatch represents a single match, pattern_id is the index of the automaton to the pattern
// offset is the byte offset in the packet where the patterns ends
typedef struct {
    int pattern_id;
    int offset;
} ACMatch;

//When we have multiple matches instead we consider this list of ACMatch.
//Note must be freed when done
typedef struct {
    ACMatch *matches;
    int      count;
    int      capacity;
} ACMatchList;

/* --- Public API --- */

//Build the automaton from an array of pattern strings. 
//Return sa heap-allocated ACAutomaton*
//Free with ac_free()
//patterns[] and their strings are copied internally, see the ACAutomaton struct
ACAutomaton *ac_build(const char **patterns, int num_patterns);

/*
 * Scan a single packet (data[0..len-1]) against the automaton.
 * Thread-safe: all mutable state is on the caller's stack.
 * Returns an ACMatchList; caller must call ac_free_matches() when done.
 */
ACMatchList ac_scan(const ACAutomaton *ac, const uint8_t *data, size_t len);

/*
 * Free an ACMatchList returned by ac_scan().
 */
void ac_free_matches(ACMatchList *ml);

/*
 * Free an ACAuto returned by ac_build().
 */
void ac_free(ACAutomaton *ac);

#endif