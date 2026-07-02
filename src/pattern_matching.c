#include "pattern_matching.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* Internal helpers */

// Allocate and initialise one fresh state node.
static void init_state(ACState *s)
{
    for (int c = 0; c < AC_ALPHABET_SIZE; c++)
        s->goto_table[c] = -1;
    s->failure = 0;
    s->output  = -1;
}

static int ensure_capacity(ACAutomaton *ac)
{
    if (ac->num_states < ac->capacity) return 1; // Plenty of space

    int new_capacity = (ac->capacity == 0) ? 1024 : ac->capacity * 2;
    
    ACState *new_states = (ACState *)realloc(ac->states, sizeof(ACState) * new_capacity);
    int *new_output = (int *)realloc(ac->output_next, sizeof(int) * new_capacity);

    if (!new_states || !new_output) return 0; // OOM

    ac->states = new_states;
    ac->output_next = new_output;
    ac->capacity = new_capacity;

    // Optional: Initialize the newly allocated states (goto_table to -1)
    for (int i = ac->num_states; i < ac->capacity; i++) {
        init_state(&ac->states[i]);
        ac->output_next[i] = -1;
    }
    return 1;
}


//Allocate a new state in the automaton
// Returns the new state's index or -1 if capacity is exhausted
static int alloc_state(ACAutomaton *ac)
{
    if (!ensure_capacity(ac)) return -1; // Allocation failed

    int id = ac->num_states++;
    // init_state is now handled inside ensure_capacity or here
    return id;
}

/* Trie Construction */
//For each pattern we walk or create a path of states in the trie, one state per character. 
// When we reach the last character the mark the node as an output node for this pattern
static void build_trie(ACAutomaton *ac)
{
    for (int p = 0; p < ac->num_patterns; p++) {
        const char *pat = ac->patterns[p];
        int len = (int)strlen(pat);
        int cur = 0; /* start from root */

        for (int i = 0; i < len; i++) {
            unsigned char c = (unsigned char)pat[i];

            if (ac->states[cur].goto_table[c] == -1) {
                /* No transition for this byte yet – create one. */
                int next = alloc_state(ac);
                if (next == -1) return; /* capacity exceeded */
                ac->states[cur].goto_table[c] = next;
            }
            cur = ac->states[cur].goto_table[c];
        }

        /*
         * cur is now the terminal state for pattern p.
         * Chain it into the output list for this state.
         * (Multiple patterns can end at the same state, e.g. "ab" and "b"
         *  if "b" also happens to end here via a suffix.)
         */
        ac->output_next[cur] = ac->states[cur].output; /* push old head */
        ac->states[cur].output = p;                    /* new head      */
    }
}

/* Failure link computation (BFS) 
/ Algorithm (standard Aho-Corasick BFS)
    - Root's failure link → root (self)
    - All depth-1 children → root
    - For deeper nodes: follow the parent's failure link until 
    you find a state that has a transition for this byte, 
    then that transition is the failure target.

    Output nodes propagation is also done by this routine:
    if a failure-target state has output, the current state inherits it
    (so we don't miss patterns that are suffixes of longer patterns).

*/
static void build_failure_links(ACAutomaton *ac)
{
    //BFS can be implemented using a simple queue (array-based) 
    // of maximum size of "num_states" (at most AC_MAX_STATES)
    int *queue = (int *)malloc(ac->num_states * sizeof(int));
    if (!queue) {
        fprintf(stderr, "[ac] ERROR: failed to allocate BFS queue\n");
        return;
    }
    int head = 0, tail = 0;

    // Initialise: for all direct children of root
    for (int c = 0; c < AC_ALPHABET_SIZE; c++) {
        int s = ac->states[0].goto_table[c];
        if (s == -1) { //No transition, set root's goto to root itself
            ac->states[0].goto_table[c] = 0;
        } else { //for nodes of depth 1, the failure link must point back to root
            ac->states[s].failure = 0;
            queue[tail++] = s;
        }
    }

    //BFS over the remaining nodes
    while (head < tail) {
        int r = queue[head++];

        for (int c = 0; c < AC_ALPHABET_SIZE; c++) {
            int s = ac->states[r].goto_table[c];
            if (s == -1) { //No transition from pattern r to btye c
                //fill the "goto" by following r's failure link
                //This turns the sparse trie into a complete DFA:
                //every (state, byte) pair now has a defined next state.
                ac->states[r].goto_table[c] = ac->states[ac->states[r].failure].goto_table[c];
                continue;
            }

            /* s is a real child – compute its failure link */
            queue[tail++] = s;

            int f = ac->states[r].failure;
            //Follow failure links until we find a valid transition for c
            //we don't need a loop here because the DFA completion above
            // ensures ac->states[f].goto_table[c] is always valid.) 
            ac->states[s].failure = ac->states[f].goto_table[c];

            /*
             * Propagate output: if the failure-target state has output,
             * append its output chain to s's output chain.
             * This handles the case where a shorter pattern is a suffix
             * of a longer one (e.g. patterns "he" and "she": when we
             * match "she" we must also report "he").
             */
            if (ac->states[ac->states[s].failure].output != -1) {
                /*
                 * Walk to the tail of s's current output chain and
                 * attach the failure state's chain there.
                 */
                if (ac->states[s].output == -1) {
                    /* s has no output yet – just borrow failure's chain */
                    ac->states[s].output = ac->states[ac->states[s].failure].output;
                } else {
                    /* Find the tail of s's chain */
                    int tail_node = ac->states[s].output;
                    while (ac->output_next[tail_node] != -1)
                        tail_node = ac->output_next[tail_node];
                    /* Attach */
                    ac->output_next[tail_node] = ac->states[ac->states[s].failure].output;
                }
            }
        }
    }

    free(queue);
}

/* AC functions */

//Initialize the automaton, requires patterns and their number
ACAutomaton *ac_build(const char **patterns, int num_patterns)
{
    ACAutomaton *ac = (ACAutomaton *)calloc(1, sizeof(ACAutomaton));
    if (!ac) return NULL;

    // Allocate memory for states and output_next
    ac->states = (ACState *)malloc(sizeof(ACState) * AC_MAX_STATES);
    ac->output_next = (int *)malloc(sizeof(int) * AC_MAX_STATES);

    /* Copy pattern strings */
    ac->num_patterns = num_patterns;
    ac->patterns = (char **)malloc(num_patterns * sizeof(char *));
    if (!ac->patterns) { free(ac); return NULL; }

    for (int i = 0; i < num_patterns; i++) {
        ac->patterns[i] = strdup(patterns[i]);
        if (!ac->patterns[i]) {
            /* Cleanup on OOM */
            for (int j = 0; j < i; j++) free(ac->patterns[j]);
            free(ac->patterns);
            free(ac);
            return NULL;
        }
    }

    /* Allocate root state (index 0) */
    ac->num_states = 0;
    alloc_state(ac); /* state 0 = root */

    build_trie(ac);
    build_failure_links(ac);

    return ac;
}


/*  This function is fully thread-safe:
    - ac is read-only (const pointer)
    - the only mutable state is `cur` (current automaton state) which lives
      on the caller's stack
    - ACMatchList is allocated on the heap but owned entirely by the caller
 
Multiple OpenMP threads can call ac_scan() concurrently on the same ACAutomaton* without any locks.
 */
ACMatchList ac_scan(const ACAutomaton *ac, const uint8_t *data, size_t len)
{
    ACMatchList ml;
    ml.count    = 0;
    ml.capacity = 16; /* initial capacity; doubled on overflow */
    ml.matches  = (ACMatch *)malloc(ml.capacity * sizeof(ACMatch));
    /* If malloc fails we return an empty list – caller should check count */
    if (!ml.matches) { ml.capacity = 0; return ml; }

    int cur = 0; /* current state – lives on the stack, thread-local */

    for (size_t i = 0; i < len; i++) {
        unsigned char c = data[i];

        /*
         * Because build_failure_links() completed the DFA (every
         * (state, byte) has a defined transition), this is just one
         * table lookup – no loop needed.
         */
        cur = ac->states[cur].goto_table[c];

        /*
         * Check for outputs at this state.
         * Walk the output chain (which may include inherited outputs
         * from failure links for suffix patterns).
         */
        int out = ac->states[cur].output;
        while (out != -1) {
            /* Grow the match list if needed */
            if (ml.count == ml.capacity) {
                ml.capacity *= 2;
                ACMatch *tmp = (ACMatch *)realloc(ml.matches,
                                                  ml.capacity * sizeof(ACMatch));
                if (!tmp) {
                    /* Out of memory – return what we have so far */
                    return ml;
                }
                ml.matches = tmp;
            }
            ml.matches[ml.count].pattern_id = out;
            ml.matches[ml.count].offset     = (int)i;
            ml.count++;

            out = ac->output_next[out];
        }
    }

    return ml;
}


// Cleanup functions
void ac_free_matches(ACMatchList *ml)
{
    if (ml && ml->matches) {
        free(ml->matches);
        ml->matches  = NULL;
        ml->count    = 0;
        ml->capacity = 0;
    }
}

void ac_free(ACAutomaton *ac)
{
    if (!ac) return;
    if (ac->patterns) {
        for (int i = 0; i < ac->num_patterns; i++)
            free(ac->patterns[i]);
        free(ac->patterns);
    }
    free(ac);
}

void ac_matchlist_init(ACMatchList *ml, int initial_capacity)
{
    if (initial_capacity <= 0) initial_capacity = 16;
    ml->count    = 0;
    ml->capacity = initial_capacity;
    ml->matches  = (ACMatch *)malloc(initial_capacity * sizeof(ACMatch));
    if (!ml->matches) ml->capacity = 0; // caller should check before use
}

void ac_scan_into(const ACAutomaton *ac, const uint8_t *data, size_t len, ACMatchList *ml)
{
    ml->count = 0; // reuse existing buffer — no malloc/free here

    int cur = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = data[i];
        cur = ac->states[cur].goto_table[c];

        int out = ac->states[cur].output;
        while (out != -1) {
            if (ml->count == ml->capacity) {
                int new_capacity = (ml->capacity > 0) ? ml->capacity * 2 : 16;
                ACMatch *tmp = (ACMatch *)realloc(ml->matches, new_capacity * sizeof(ACMatch));
                if (!tmp) return; // keep whatever matches we already have
                ml->matches  = tmp;
                ml->capacity = new_capacity;
            }
            ml->matches[ml->count].pattern_id = out;
            ml->matches[ml->count].offset     = (int)i;
            ml->count++;
            out = ac->output_next[out];
        }
    }
}
