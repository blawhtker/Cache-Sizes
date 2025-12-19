// This program reads a trace file with memory reads/writes
// and simulates how a cache would behave.
// It prints how many cache misses happen, and how many reads/writes go to memory.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

// Each block in the cache is 64 bytes.
// This is fixed because the assignment said to assume 64B blocks.
#define BLOCK_SIZE 64ULL

// This struct is one "slot" in the cache (a cache line)
typedef struct {
    bool valid;                       // true if there is actually data stored here
    bool dirty;                       // true if the data was changed but not yet written to memory (only for write-back)
    unsigned long long tag;           // used to tell if the stored block matches the memory address
    unsigned long long lru_ts;        // keeps track of when this line was last used (for LRU)
    unsigned long long fifo_ts;       // keeps track of when this line was added (for FIFO)
} line_t;

// A set is just a small group of cache lines.
// The number of lines in a set is the "associativity".
typedef struct {
    line_t *ways;
} set_t;

// This struct represents the entire cache.
typedef struct {
    size_t cache_size;      // total size of cache in bytes
    size_t assoc;           // how many lines per set (associativity)
    size_t num_sets;        // total sets = cache_size / (BLOCK_SIZE * assoc)
    int replacement;        // 0 = LRU (least recently used), 1 = FIFO (first in first out)
    int writeback;          // 0 = write-through, 1 = write-back

    set_t *sets;            // the array of sets

    // These keep track of statistics
    unsigned long long hits;
    unsigned long long misses;
    unsigned long long mem_reads;
    unsigned long long mem_writes;

    unsigned long long global_ts; // increases each time we access the cache
} cache_t;

// Figure out which set a memory address belongs to.
static inline unsigned long long get_set_index(unsigned long long addr, size_t num_sets) {
    return (addr / BLOCK_SIZE) % num_sets;
}

// Figure out the "tag" of a memory address.
// This is what identifies which block of memory we’re talking about.
static inline unsigned long long get_tag(unsigned long long addr, size_t num_sets) {
    unsigned long long block_number = addr / BLOCK_SIZE;
    return block_number / num_sets;
}

// Make the cache with the right number of sets and lines
static cache_t *cache_create(size_t cache_size, size_t assoc, int replacement, int writeback) {
    cache_t *c = (cache_t*)calloc(1, sizeof(cache_t));
    if (!c) return NULL;

    c->cache_size = cache_size;
    c->assoc = assoc;
    c->replacement = replacement;
    c->writeback = writeback;

    if (assoc == 0) { free(c); return NULL; }

    size_t lines = cache_size / BLOCK_SIZE;
    if (lines == 0 || lines % assoc != 0) {
        // this checks that the cache divides evenly into sets
        free(c);
        return NULL;
    }

    c->num_sets = lines / assoc;

    // make space for all the sets
    c->sets = (set_t*)calloc(c->num_sets, sizeof(set_t));
    if (!c->sets) { free(c); return NULL; }

    for (size_t s = 0; s < c->num_sets; ++s) {
        c->sets[s].ways = (line_t*)calloc(assoc, sizeof(line_t));
        if (!c->sets[s].ways) {
            for (size_t k = 0; k < s; ++k) free(c->sets[k].ways);
            free(c->sets);
            free(c);
            return NULL;
        }
    }
    return c;
}

// Clean up memory when we’re done
static void cache_destroy(cache_t *c) {
    if (!c) return;
    if (c->sets) {
        for (size_t s = 0; s < c->num_sets; ++s)
            free(c->sets[s].ways);
        free(c->sets);
    }
    free(c);
}

// Choose which line to replace when the cache is full.
// If there’s an empty one, use it. Otherwise pick one based on the rule (LRU or FIFO).
static size_t select_victim(cache_t *c, size_t set_idx) {
    set_t *set = &c->sets[set_idx];

    // look for an empty spot first
    for (size_t w = 0; w < c->assoc; ++w) {
        if (!set->ways[w].valid) return w;
    }

    // if none are empty, pick based on replacement policy
    size_t victim = 0;
    unsigned long long best = ULLONG_MAX;

    if (c->replacement == 0) { // LRU
        for (size_t w = 0; w < c->assoc; ++w) {
            if (set->ways[w].lru_ts < best) {
                best = set->ways[w].lru_ts;
                victim = w;
            }
        }
    } else { // FIFO
        for (size_t w = 0; w < c->assoc; ++w) {
            if (set->ways[w].fifo_ts < best) {
                best = set->ways[w].fifo_ts;
                victim = w;
            }
        }
    }
    return victim;
}

// When we hit something that’s already in the cache, update its timestamp (for LRU).
static void update_on_hit(cache_t *c, size_t set_idx, size_t way_idx) {
    line_t *ln = &c->sets[set_idx].ways[way_idx];
    if (c->replacement == 0) {
        ln->lru_ts = ++c->global_ts;
    }
}

// Store a new block into the cache after we choose where it goes.
static void fill_line(cache_t *c, size_t set_idx, size_t way_idx, unsigned long long tag, bool make_dirty) {
    line_t *ln = &c->sets[set_idx].ways[way_idx];
    ln->valid = true;
    ln->tag = tag;
    ln->dirty = make_dirty;

    unsigned long long now = ++c->global_ts;
    ln->lru_ts = now;
    ln->fifo_ts = now;
}

// If the line we’re removing was dirty (changed), we need to write it back to memory.
static void evict_if_needed(cache_t *c, size_t set_idx, size_t way_idx) {
    line_t *ln = &c->sets[set_idx].ways[way_idx];
    if (ln->valid && c->writeback == 1 && ln->dirty) {
        c->mem_writes++;
    }
}

// This runs for each read or write in the trace file.
static void cache_access(cache_t *c, char op, unsigned long long addr) {
    unsigned long long tag = get_tag(addr, c->num_sets);
    size_t set_idx = get_set_index(addr, c->num_sets);
    set_t *set = &c->sets[set_idx];

    // check if it’s already in the cache (a hit)
    for (size_t w = 0; w < c->assoc; ++w) {
        if (set->ways[w].valid && set->ways[w].tag == tag) {
            c->hits++;
            update_on_hit(c, set_idx, w);

            // handle writes
            if (op == 'W' || op == 'w') {
                if (c->writeback == 1) {
                    // for write-back, mark dirty and don’t write right now
                    set->ways[w].dirty = true;
                } else {
                    // for write-through, write to memory right away
                    c->mem_writes++;
                }
            }
            return;
        }
    }

    // if we didn’t find it, that’s a miss
    c->misses++;

    if (op == 'R' || op == 'r') {
        // read miss means we bring the block from memory into the cache
        size_t victim = select_victim(c, set_idx);
        evict_if_needed(c, set_idx, victim);
        c->mem_reads++;
        fill_line(c, set_idx, victim, tag, false);
    } else { // write miss
        if (c->writeback == 1) {
            // write-back: bring it in (write-allocate), then mark dirty
            size_t victim = select_victim(c, set_idx);
            evict_if_needed(c, set_idx, victim);
            c->mem_reads++;
            fill_line(c, set_idx, victim, tag, true);
        } else {
            // write-through: don’t bring it in (no-write-allocate), just write directly
            c->mem_writes++;
        }
    }
}

int main(int argc, char **argv) {
    // check that the arguments are correct
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <CACHE_SIZE> <ASSOC> <REPLACEMENT> <WB> <TRACE_FILE>\n", argv[0]);
        return 1;
    }

    unsigned long long cache_size = strtoull(argv[1], NULL, 10);
    unsigned long long assoc      = strtoull(argv[2], NULL, 10);
    int replacement               = atoi(argv[3]); // 0 = LRU, 1 = FIFO
    int writeback                 = atoi(argv[4]); // 0 = write-through, 1 = write-back
    const char *trace_path        = argv[5];

    // make sure we got valid numbers
    if (cache_size == 0 || assoc == 0) {
        fprintf(stderr, "Invalid cache size or associativity.\n");
        return 1;
    }

    cache_t *cache = cache_create((size_t)cache_size, (size_t)assoc, replacement, writeback);
    if (!cache) {
        fprintf(stderr, "Could not set up cache.\n");
        return 1;
    }

    // open the trace file
    FILE *fp = fopen(trace_path, "r");
    if (!fp) {
        fprintf(stderr, "Error: could not open the trace file: %s\n", trace_path);
        cache_destroy(cache);
        return 1;
    }

    char op;
    unsigned long long addr;
    // read one line at a time: operation (R or W) and address
    while (fscanf(fp, " %c %llx", &op, &addr) == 2) {
        cache_access(cache, op, addr);
    }
    fclose(fp);

    // figure out the miss ratio (misses divided by total accesses)
    unsigned long long total = cache->hits + cache->misses;
    double miss_ratio = (total > 0) ? (double)cache->misses / (double)total : 0.0;

    // print the results
    printf("Miss ratio %f\n", miss_ratio);
    printf("write %llu\n", cache->mem_writes);
    printf("read %llu\n", cache->mem_reads);

    cache_destroy(cache);
    return 0;
}
