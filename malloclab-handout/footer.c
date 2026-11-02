/*
 * Explicit segregated-list allocator using a prev_alloc header bit.
 *
 * Every modification made for the prev_alloc/footer optimization is marked
 * with "PA CHANGE".  Normal allocated blocks have no footer.  Free blocks
 * still have a footer so that a following block can move backward when its
 * prev_alloc bit is clear.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

team_t team = {
    "ateam",
    "Harry Bovik",
    "bovik@cs.cmu.edu",
    "",
    ""
};

#define NEXT_FITx
#define FIRST_FITx

#define WSIZE       4
#define DSIZE       8
#define CHUNKSIZE  (1 << 8)
#define ALIGNMENT   8
#define LISTNUM     12

#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

/* A free block must hold header, footer, prev pointer, and next pointer. */
#define MINBLOCK ALIGN(DSIZE + 2 * sizeof(void *))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

/*
 * PA CHANGE 1: header bit layout.
 * bit 0 = this block is allocated
 * bit 1 = previous physical block is allocated
 */
#define PACK(size, alloc, prev_alloc) \
    ((size) | ((alloc) ? 0x1 : 0) | ((prev_alloc) ? 0x2 : 0))

#define GET(p)       (*(unsigned int *)(p))
#define PUT(p, val)  (*(unsigned int *)(p) = (val))

#define GET_SIZE(p)       (GET(p) & ~0x7)
#define GET_ALLOC(p)      (GET(p) & 0x1)
/* PA CHANGE 2: read and update the preceding-block state in a header. */
#define GET_PREV_ALLOC(p) (GET(p) & 0x2)
#define SET_PREV_ALLOC(p)   PUT((p), GET(p) | 0x2)
#define CLEAR_PREV_ALLOC(p) PUT((p), GET(p) & ~0x2)

#define HDRP(bp) ((char *)(bp) - WSIZE)

/* PA CHANGE 3: FTRP is valid only for free blocks (plus the prologue sentinel). */
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp) \
    ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))

/* PA CHANGE 4: call PREV_BLKP only when GET_PREV_ALLOC(HDRP(bp)) is zero. */
#define PREV_BLKP(bp) \
    ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

#define NEXT_FREE(bp) (*(void **)(bp))
#define PREV_FREE(bp) (*(void **)((char *)(bp) + sizeof(void *)))

static char *heap_listp = NULL;
static void *free_listp[LISTNUM];

#ifdef NEXT_FIT
static char *rover;
#endif

#define DEBUG 0

#if DEBUG
#define CHECKHEAP() checkheap(0)
#else
#define CHECKHEAP() ((void)0)
#endif

static void *extend_heap(size_t words);
static void *place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void printblock(void *bp);
static void checkheap(int verbose);
static void checkblock(void *bp);
static void insert_free_block(void *bp);
static void remove_free_block(void *bp);
static int get_list_index(size_t size);

static int get_list_index(size_t size)
{
    int index = 0;

    while (size > MINBLOCK && index < LISTNUM - 1) {
        size = (size + 1) >> 1;
        index++;
    }
    return index;
}

static void insert_free_block(void *bp)
{
    int index = get_list_index(GET_SIZE(HDRP(bp)));

    PREV_FREE(bp) = NULL;
    NEXT_FREE(bp) = free_listp[index];
    if (free_listp[index] != NULL)
        PREV_FREE(free_listp[index]) = bp;
    free_listp[index] = bp;
}

static void remove_free_block(void *bp)
{
    int index = get_list_index(GET_SIZE(HDRP(bp)));
    void *prev = PREV_FREE(bp);
    void *next = NEXT_FREE(bp);

    if (prev != NULL)
        NEXT_FREE(prev) = next;
    else
        free_listp[index] = next;

    if (next != NULL)
        PREV_FREE(next) = prev;
}

int mm_init(void)
{
    heap_listp = NULL;
    for (int i = 0; i < LISTNUM; i++)
        free_listp[i] = NULL;

    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;

    /*
     * PA CHANGE 5: initialize prev_alloc in the sentinels.
     * The prologue keeps its traditional footer as a special sentinel; normal
     * allocated blocks created later do not have footers.  The initial
     * epilogue says its preceding prologue is allocated.
     */
    PUT(heap_listp, 0);
    PUT(heap_listp + WSIZE,     PACK(DSIZE, 1, 1));
    PUT(heap_listp + 2 * WSIZE, PACK(DSIZE, 1, 1));
    PUT(heap_listp + 3 * WSIZE, PACK(0, 1, 1));
    heap_listp += 2 * WSIZE;

#ifdef NEXT_FIT
    rover = heap_listp;
#endif

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;

    CHECKHEAP();
    return 0;
}

void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char *bp;

    if (heap_listp == NULL && mm_init() == -1)
        return NULL;
    if (size == 0)
        return NULL;

    /*
     * PA CHANGE 6: an allocated block pays for one header, not a
     * header+footer pair.  MINBLOCK is retained because the block must be able
     * to hold free-list links and a footer after it is freed.
     */
    asize = MAX(MINBLOCK, ALIGN(size + WSIZE));

    bp = find_fit(asize);
    if (bp != NULL) {
        bp = place(bp, asize);
        CHECKHEAP();
        return bp;
    }

    extendsize = MAX(asize, CHUNKSIZE);
    bp = extend_heap(extendsize / WSIZE);
    if (bp == NULL)
        return NULL;

    bp = place(bp, asize);
    CHECKHEAP();
    return bp;
}

void mm_free(void *bp)
{
    size_t size;
    size_t prev_alloc;

    if (bp == NULL)
        return;

    size = GET_SIZE(HDRP(bp));
    prev_alloc = GET_PREV_ALLOC(HDRP(bp));

    /*
     * PA CHANGE 7: freeing creates the footer and clears the next block's
     * prev_alloc bit.  A free footer stores size/alloc only; prev_alloc belongs
     * to the header.
     */
    PUT(HDRP(bp), PACK(size, 0, prev_alloc));
    PUT(FTRP(bp), PACK(size, 0, 0));
    CLEAR_PREV_ALLOC(HDRP(NEXT_BLKP(bp)));

    coalesce(bp);
    CHECKHEAP();
}

/*
 * PA CHANGE 8: coalescing learns the previous block's state from this block's
 * header.  It reads the preceding footer and calls PREV_BLKP only when that bit
 * is clear.  All old neighbors are removed before metadata changes, and the
 * merged block is inserted exactly once.
 */
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    void *next_bp = NEXT_BLKP(bp);
    size_t next_alloc = GET_ALLOC(HDRP(next_bp));
    size_t size = GET_SIZE(HDRP(bp));
    void *prev_bp = NULL;

    if (!prev_alloc) {
        prev_bp = PREV_BLKP(bp);
        remove_free_block(prev_bp);
        size += GET_SIZE(HDRP(prev_bp));
    }

    if (!next_alloc) {
        remove_free_block(next_bp);
        size += GET_SIZE(HDRP(next_bp));
    }

    if (!prev_alloc)
        bp = prev_bp;

    prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0, prev_alloc));
    PUT(FTRP(bp), PACK(size, 0, 0));

    /* The physical block following the merged free block has a free predecessor. */
    CLEAR_PREV_ALLOC(HDRP(NEXT_BLKP(bp)));
    insert_free_block(bp);

#ifdef NEXT_FIT
    if (rover > (char *)bp && rover < NEXT_BLKP(bp))
        rover = bp;
#endif

    return bp;
}

void *mm_realloc(void *ptr, size_t size)
{
    size_t oldsize;
    size_t asize;
    size_t ptr_prev_alloc;
    void *next_bp;
    void *newptr;

    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }
    if (ptr == NULL)
        return mm_malloc(size);

    /* PA CHANGE 9: realloc uses the same one-header adjusted size as malloc. */
    asize = MAX(MINBLOCK, ALIGN(size + WSIZE));
    oldsize = GET_SIZE(HDRP(ptr));
    ptr_prev_alloc = GET_PREV_ALLOC(HDRP(ptr));

    if (asize <= oldsize) {
        size_t remainsize = oldsize - asize;

        if (remainsize >= MINBLOCK) {
            /* PA CHANGE 10: allocated half has no footer; new free half does. */
            PUT(HDRP(ptr), PACK(asize, 1, ptr_prev_alloc));

            void *free_bp = NEXT_BLKP(ptr);
            PUT(HDRP(free_bp), PACK(remainsize, 0, 1));
            PUT(FTRP(free_bp), PACK(remainsize, 0, 0));
            CLEAR_PREV_ALLOC(HDRP(NEXT_BLKP(free_bp)));
            coalesce(free_bp);
        }
        return ptr;
    }

    next_bp = NEXT_BLKP(ptr);
    if (!GET_ALLOC(HDRP(next_bp)) &&
        oldsize + GET_SIZE(HDRP(next_bp)) >= asize) {
        size_t total_size = oldsize + GET_SIZE(HDRP(next_bp));
        size_t remainsize = total_size - asize;

        remove_free_block(next_bp);

        if (remainsize >= MINBLOCK) {
            /* PA CHANGE 11: consume the next free block without adding a footer to ptr. */
            PUT(HDRP(ptr), PACK(asize, 1, ptr_prev_alloc));

            void *free_bp = NEXT_BLKP(ptr);
            PUT(HDRP(free_bp), PACK(remainsize, 0, 1));
            PUT(FTRP(free_bp), PACK(remainsize, 0, 0));
            CLEAR_PREV_ALLOC(HDRP(NEXT_BLKP(free_bp)));
            coalesce(free_bp);
        } else {
            PUT(HDRP(ptr), PACK(total_size, 1, ptr_prev_alloc));
            SET_PREV_ALLOC(HDRP(NEXT_BLKP(ptr)));
        }
        return ptr;
    }

    /*
     * PA CHANGE 12: epilogue cannot be tested with raw header == 1 anymore;
     * it is normally 3 when its preceding block is allocated.
     */
    if (GET_SIZE(HDRP(next_bp)) == 0) {
        size_t extendsize = MAX(asize - oldsize, MINBLOCK);
        void *extension = extend_heap(extendsize / WSIZE);

        if (extension != NULL) {
            size_t extension_size = GET_SIZE(HDRP(extension));

            remove_free_block(extension);
            PUT(HDRP(ptr), PACK(oldsize + extension_size, 1,
                                 ptr_prev_alloc));
            SET_PREV_ALLOC(HDRP(NEXT_BLKP(ptr)));
            return ptr;
        }
    }

    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;

    /* PA CHANGE 13: old allocated payload capacity is oldsize - WSIZE. */
    {
        size_t copy_size = oldsize - WSIZE;
        if (size < copy_size)
            copy_size = size;
        memcpy(newptr, ptr, copy_size);
    }

    mm_free(ptr);
    return newptr;
}

void mm_checkheap(int verbose)
{
    checkheap(verbose);
}

static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;
    size_t prev_alloc;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((bp = mem_sbrk(size)) == (void *)-1)
        return NULL;

    /*
     * PA CHANGE 14: HDRP(bp) is the old epilogue.  Preserve its prev_alloc
     * value before overwriting it with the new free-block header.  The new
     * epilogue records that its predecessor is free.
     */
    prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0, prev_alloc));
    PUT(FTRP(bp), PACK(size, 0, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1, 0));

    return coalesce(bp);
}

/*
 * Keep the original high-scoring policy: leave the free remainder at the
 * front and allocate from the back of the selected block.
 */
static void *place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));

    remove_free_block(bp);

    if (csize - asize >= MINBLOCK) {
        size_t remainsize = csize - asize;
        void *allocated_bp;

        /* PA CHANGE 15: only the free remainder receives a footer. */
        PUT(HDRP(bp), PACK(remainsize, 0, prev_alloc));
        PUT(FTRP(bp), PACK(remainsize, 0, 0));

        allocated_bp = NEXT_BLKP(bp);
        PUT(HDRP(allocated_bp), PACK(asize, 1, 0));

        /* The block after allocated_bp now has an allocated predecessor. */
        SET_PREV_ALLOC(HDRP(NEXT_BLKP(allocated_bp)));
        insert_free_block(bp);
        return allocated_bp;
    }

    /* PA CHANGE 16: whole-block allocation removes/ignores the old footer. */
    PUT(HDRP(bp), PACK(csize, 1, prev_alloc));
    SET_PREV_ALLOC(HDRP(NEXT_BLKP(bp)));
    return bp;
}

static void *find_fit(size_t asize)
{
#ifdef NEXT_FIT
    char *oldrover = rover;

    for (; GET_SIZE(HDRP(rover)) > 0; rover = NEXT_BLKP(rover))
        if (!GET_ALLOC(HDRP(rover)) && asize <= GET_SIZE(HDRP(rover)))
            return rover;

    for (rover = heap_listp; rover < oldrover; rover = NEXT_BLKP(rover))
        if (!GET_ALLOC(HDRP(rover)) && asize <= GET_SIZE(HDRP(rover)))
            return rover;

    return NULL;
#elif defined(FIRST_FIT)
    int index = get_list_index(asize);

    for (int i = index; i < LISTNUM; i++) {
        for (void *bp = free_listp[i]; bp != NULL; bp = NEXT_FREE(bp))
            if (asize <= GET_SIZE(HDRP(bp)))
                return bp;
    }
    return NULL;
#else
    int index = get_list_index(asize);

    for (int i = index; i < LISTNUM; i++) {
        void *target = NULL;
        size_t best_diff = (size_t)-1;

        for (void *bp = free_listp[i]; bp != NULL; bp = NEXT_FREE(bp)) {
            size_t block_size = GET_SIZE(HDRP(bp));

            if (block_size >= asize && block_size - asize < best_diff) {
                best_diff = block_size - asize;
                target = bp;
                if (best_diff == 0)
                    return bp;
            }
        }

        if (target != NULL)
            return target;
    }
    return NULL;
#endif
}

/* PA CHANGE 17: never read a footer from an allocated block. */
static void printblock(void *bp)
{
    size_t hsize = GET_SIZE(HDRP(bp));
    int halloc = !!GET_ALLOC(HDRP(bp));
    int prev_alloc = !!GET_PREV_ALLOC(HDRP(bp));

    if (hsize == 0) {
        printf("%p: epilogue: [prev=%c alloc=a]\n", bp,
               prev_alloc ? 'a' : 'f');
        return;
    }

    if (halloc) {
        printf("%p: header: [%zu prev=%c alloc=a] (no footer)\n",
               bp, hsize, prev_alloc ? 'a' : 'f');
    } else {
        printf("%p: header: [%zu prev=%c alloc=f] footer: [%u:f]\n",
               bp, hsize, prev_alloc ? 'a' : 'f',
               GET_SIZE(FTRP(bp)));
    }
}

/* PA CHANGE 18: header/footer equality is checked only for free blocks. */
static void checkblock(void *bp)
{
    if ((size_t)bp % ALIGNMENT)
        printf("Error: %p is not doubleword aligned\n", bp);

    if (!GET_ALLOC(HDRP(bp)) &&
        (GET_SIZE(HDRP(bp)) != GET_SIZE(FTRP(bp)) ||
         GET_ALLOC(FTRP(bp)))) {
        printf("Error: free block %p header does not match footer\n", bp);
    }
}

static void checkheap(int verbose)
{
    char *bp;
    int expected_prev_alloc = 1;

    if (verbose)
        printf("Heap (%p):\n", heap_listp);

    /* PA CHANGE 19: validate both allocation bits in the prologue. */
    if (GET_SIZE(HDRP(heap_listp)) != DSIZE ||
        !GET_ALLOC(HDRP(heap_listp)) ||
        !GET_PREV_ALLOC(HDRP(heap_listp))) {
        printf("Bad prologue header\n");
    }

    /* Detect cycles before any ordinary traversal of a free list. */
    for (int i = 0; i < LISTNUM; i++) {
        char *slow = free_listp[i];
        char *fast = free_listp[i];

        while (fast != NULL && NEXT_FREE(fast) != NULL) {
            slow = NEXT_FREE(slow);
            fast = NEXT_FREE(NEXT_FREE(fast));
            if (slow == fast) {
                printf("Heap error: cycle detected in free list %d at block %p\n",
                       i, (void *)slow);
                return;
            }
        }
    }

    /*
     * PA CHANGE 20: check that each header's prev_alloc bit agrees with the
     * actual preceding physical block.  checkblock itself reads a footer only
     * for a free block.
     */
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        int recorded_prev_alloc = !!GET_PREV_ALLOC(HDRP(bp));

        if (recorded_prev_alloc != expected_prev_alloc) {
            printf("Heap error: bad prev_alloc bit at block %p\n", (void *)bp);
        }

        if (verbose)
            printblock(bp);
        checkblock(bp);
        expected_prev_alloc = !!GET_ALLOC(HDRP(bp));
    }

    /* PA CHANGE 21: epilogue also carries the preceding block's state. */
    if (GET_SIZE(HDRP(bp)) != 0 || !GET_ALLOC(HDRP(bp)))
        printf("Bad epilogue header\n");
    if (!!GET_PREV_ALLOC(HDRP(bp)) != expected_prev_alloc)
        printf("Heap error: bad epilogue prev_alloc bit\n");
    if (verbose)
        printblock(bp);

    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp))) {
            int found = 0;

            for (int i = 0; i < LISTNUM && !found; i++) {
                for (char *node = free_listp[i];
                     node != NULL;
                     node = NEXT_FREE(node)) {
                    if (node == bp) {
                        found = 1;
                        break;
                    }
                }
            }

            if (!found)
                printf("Heap error: free block %p is missing from free list\n",
                       (void *)bp);

            if (GET_SIZE(HDRP(NEXT_BLKP(bp))) > 0 &&
                !GET_ALLOC(HDRP(NEXT_BLKP(bp)))) {
                printf("Heap error: adjacent free blocks at %p and %p were not coalesced\n",
                       (void *)bp, (void *)NEXT_BLKP(bp));
            }
        }
    }

    for (int i = 0; i < LISTNUM; i++) {
        if (free_listp[i] != NULL && PREV_FREE(free_listp[i]) != NULL)
            printf("Heap error: free-list %d head has non-NULL prev pointer\n", i);

        for (bp = free_listp[i]; bp != NULL; bp = NEXT_FREE(bp)) {
            size_t block_size = GET_SIZE(HDRP(bp));

            if (get_list_index(block_size) != i) {
                printf("Heap error: free block %p of size %zu is in wrong list %d\n",
                       (void *)bp, block_size, i);
            }
            if (GET_ALLOC(HDRP(bp))) {
                printf("Heap error: allocated block %p found in free list %d\n",
                       (void *)bp, i);
            }
            if (NEXT_FREE(bp) != NULL &&
                PREV_FREE(NEXT_FREE(bp)) != bp) {
                printf("Heap error: free-list next/prev mismatch at block %p\n",
                       (void *)bp);
            }
            if (PREV_FREE(bp) != NULL &&
                NEXT_FREE(PREV_FREE(bp)) != bp) {
                printf("Heap error: free-list prev/next mismatch at block %p\n",
                       (void *)bp);
            }
        }
    }
}
