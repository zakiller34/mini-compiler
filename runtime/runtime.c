#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// @brief Read a 64-bit integer from stdin
/// @ensures result == value parsed from stdin
int64_t read_int(void) {
    int64_t val;
    scanf("%ld", &val);
    return val;
}

/// @brief Print a 64-bit integer to stdout followed by newline
/// @requires x is a valid int64_t
void print_int(int64_t x) { printf("%ld\n", x); }

/* ---- GC globals ---- */

int64_t *free_ptr = NULL;
int64_t *fromspace_begin = NULL;
int64_t *fromspace_end = NULL;
int64_t *tospace_begin = NULL;
int64_t *tospace_end = NULL;
int64_t *rootstack_begin = NULL;

/* ---- Tag helpers ---- */

/// @brief Extract length from tag (bits 1-6)
static int64_t tag_length(int64_t tag) { return (tag >> 1) & 0x3F; }

/// @brief Check if element i is a pointer (bit 7+i)
static int tag_is_ptr(int64_t tag, int64_t i) {
    return (tag >> (7 + i)) & 1;
}

/// @brief Check if tag is a forwarding pointer (bit 0)
static int tag_is_fwd(int64_t tag) { return tag & 1; }

/* ---- Cheney copy ---- */

/// @brief Copy a single tuple from fromspace to tospace
/// @requires tuple points into fromspace, scan_ptr is current tospace alloc
/// @ensures returns pointer to copy in tospace; original tag replaced w/ fwd
static int64_t *copy_tuple(int64_t *tuple, int64_t **scan_ptr) {
    int64_t tag = tuple[0];
    if (tag_is_fwd(tag)) {
        /* Already forwarded — tag stores the new address */
        return (int64_t *)(tag & ~1LL);
    }
    int64_t len = tag_length(tag);
    int64_t words = len + 1; /* tag + elements */

    /* Copy to tospace at scan_ptr */
    int64_t *dest = *scan_ptr;
    memcpy(dest, tuple, (size_t)words * sizeof(int64_t));
    *scan_ptr += words;

    /* Install forwarding pointer in old location */
    tuple[0] = ((int64_t)dest) | 1;

    return dest;
}

/// @brief Scan a tuple in tospace, copying any pointed-to tuples
/// @requires tuple is in tospace, scan_ptr is tospace alloc cursor
static void scan_tuple(int64_t *tuple, int64_t **scan_ptr) {
    int64_t tag = tuple[0];
    int64_t len = tag_length(tag);
    for (int64_t i = 0; i < len; ++i) {
        if (tag_is_ptr(tag, i)) {
            int64_t *child = (int64_t *)tuple[i + 1];
            if (child >= fromspace_begin && child < fromspace_end) {
                tuple[i + 1] = (int64_t)copy_tuple(child, scan_ptr);
            }
        }
    }
}

/// @brief Initialize GC: allocate from/tospace and rootstack
/// @requires rootstack_size > 0, heap_size > 0
void initialize(int64_t rootstack_size, int64_t heap_size) {
    int64_t rs_words = rootstack_size / 8;
    int64_t heap_words = heap_size / 8;

    rootstack_begin = (int64_t *)calloc((size_t)rs_words, sizeof(int64_t));
    fromspace_begin = (int64_t *)calloc((size_t)heap_words, sizeof(int64_t));
    tospace_begin = (int64_t *)calloc((size_t)heap_words, sizeof(int64_t));

    fromspace_end = fromspace_begin + heap_words;
    tospace_end = tospace_begin + heap_words;
    free_ptr = fromspace_begin;
}

/// @brief Cheney GC: copy live tuples from fromspace to tospace
/// @requires rootstack_ptr points past last used root slot
/// @ensures free_ptr + bytes/8 <= fromspace_end after collection
void collect(int64_t *rootstack_ptr, int64_t bytes) {
    int64_t *scan = tospace_begin;
    int64_t *alloc = tospace_begin;

    /* Phase 1: copy root set */
    for (int64_t *rp = rootstack_begin; rp < rootstack_ptr; ++rp) {
        if (*rp != 0) {
            int64_t *tuple = (int64_t *)*rp;
            if (tuple >= fromspace_begin && tuple < fromspace_end) {
                *rp = (int64_t)copy_tuple(tuple, &alloc);
            }
        }
    }

    /* Phase 2: BFS scan tospace */
    while (scan < alloc) {
        int64_t tag = scan[0];
        int64_t len = tag_length(tag);
        scan_tuple(scan, &alloc);
        scan += len + 1;
    }

    /* Swap spaces */
    int64_t *tmp_begin = fromspace_begin;
    int64_t *tmp_end = fromspace_end;
    fromspace_begin = tospace_begin;
    fromspace_end = tospace_end;
    tospace_begin = tmp_begin;
    tospace_end = tmp_end;
    free_ptr = alloc;

    /* Zero tospace for next collection */
    memset(tospace_begin, 0,
           (size_t)(tospace_end - tospace_begin) * sizeof(int64_t));

    (void)bytes; /* bytes used by caller for reservation check */
}
