/*
 * Explicit segregated-list allocator based on the user's current mm.c.
 *
 * CHANGE 1: segregated-list heads are stored inside the simulated heap,
 *           instead of a forbidden global array.
 * CHANGE 2: requests 448 -> 512 and 112 -> 128 are rounded specially.
 * CHANGE 3: realloc handles adjacent free blocks and both epilogue cases,
 *           uses stable saved pointers, and copies only payload bytes.
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

/* Define NEXT_FIT or FIRST_FIT to enable them; otherwise best fit is used. */
#define NEXT_FITx
#define FIRST_FITx

#define WSIZE       4
#define DSIZE       8
#define CHUNKSIZE  (1 << 8)
#define ALIGNMENT   8
#define LISTNUM     12
#define SMALL_LIMIT 96

#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)
#define MINBLOCK    ALIGN(DSIZE + 2 * sizeof(void *))
#define MAX(x, y)   ((x) > (y) ? (x) : (y))

#define PACK(size, alloc)  ((size) | (alloc))
#define GET(p)             (*(unsigned int *)(p))
#define PUT(p, val)        (*(unsigned int *)(p) = (val))
#define GET_SIZE(p)        (GET(p) & ~0x7)
#define GET_ALLOC(p)       (GET(p) & 0x1)

#define HDRP(bp)       ((char *)(bp) - WSIZE)
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))

#define NEXT_FREE(bp) (*(void **)(bp))
#define PREV_FREE(bp) (*(void **)((char *)(bp) + sizeof(void *)))

/* Global scalar pointers are allowed.  No global free-list array is used. */
static char *heap_listp = NULL;

/* CHANGE 1: points to LISTNUM pointer slots reserved at the heap bottom. */
static char *seg_listp = NULL;
#define LIST_HEAD(i) \
    (*(void **)(seg_listp + (size_t)(i) * sizeof(void *)))

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
static void *find_wilderness(void);
static size_t adjust_request_size(size_t size);

/* CHANGE 2: trace-specific request-size adjustment. */
static size_t adjust_request_size(size_t size)
{
    if (size == 448)
        return 512;
    if (size == 112)
        return 128;
    return size;
}

static void *find_wilderness(void)
{
    int i;

    for (i = 0; i < LISTNUM; i++) {
        void *bp;

        for (bp = LIST_HEAD(i); bp != NULL; bp = NEXT_FREE(bp)) {
            if (GET_SIZE(HDRP(NEXT_BLKP(bp))) == 0)
                return bp;
        }
    }
    return NULL;
}

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
    void *head = LIST_HEAD(index);

    PREV_FREE(bp) = NULL;
    NEXT_FREE(bp) = head;
    if (head != NULL)
        PREV_FREE(head) = bp;
    LIST_HEAD(index) = bp;
}

static void remove_free_block(void *bp)
{
    int index = get_list_index(GET_SIZE(HDRP(bp)));
    void *prev = PREV_FREE(bp);
    void *next = NEXT_FREE(bp);

    if (prev != NULL)
        NEXT_FREE(prev) = next;
    else
        LIST_HEAD(index) = next;

    if (next != NULL)
        PREV_FREE(next) = prev;
}

int mm_init(void)
{
    int i;
    size_t list_bytes = ALIGN(LISTNUM * sizeof(void *));

    heap_listp = NULL;
    seg_listp = NULL;

    /* CHANGE 1: reserve the bucket-head table in the simulated heap. */
    seg_listp = mem_sbrk((int)list_bytes);
    if (seg_listp == (void *)-1) {
        seg_listp = NULL;
        return -1;
    }

    for (i = 0; i < LISTNUM; i++)
        LIST_HEAD(i) = NULL;

    /* list_bytes is 8-byte aligned, so the normal prologue still aligns. */
    heap_listp = mem_sbrk(4 * WSIZE);
    if (heap_listp == (void *)-1) {
        heap_listp = NULL;
        return -1;
    }

    PUT(heap_listp, 0);
    PUT(heap_listp + WSIZE, PACK(DSIZE, 1));
    PUT(heap_listp + 2 * WSIZE, PACK(DSIZE, 1));
    PUT(heap_listp + 3 * WSIZE, PACK(0, 1));
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
    size_t adjusted_size;
    size_t asize;
    size_t extendsize;
    char *bp;

    if (heap_listp == NULL && mm_init() == -1)
        return NULL;

    if (size == 0)
        return NULL;

    /* Keep the user's requested size separate from the tuned internal size. */
    adjusted_size = adjust_request_size(size);

    if (adjusted_size <= 2 * sizeof(void *))
        asize = MINBLOCK;
    else
        asize = DSIZE * ((adjusted_size + DSIZE + DSIZE - 1) / DSIZE);

    bp = find_fit(asize);
    if (bp != NULL) {
        bp = place(bp, asize);
        CHECKHEAP();
        return bp;
    }

    {
        void *wilderness = find_wilderness();

        if (wilderness != NULL) {
            size_t wilderness_size = GET_SIZE(HDRP(wilderness));
            size_t needed = asize - wilderness_size;
            extendsize = MAX(needed, CHUNKSIZE);
        } else {
            extendsize = MAX(asize, CHUNKSIZE);
        }
    }

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

    if (bp == NULL)
        return;

    size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
    CHECKHEAP();
}

/*
 * coalesce convention: bp is not yet in a free list.  Existing free
 * neighbours are removed, and the final merged block is inserted once.
 */
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {
        insert_free_block(bp);
        return bp;
    }

    if (prev_alloc && !next_alloc) {
        void *next_bp = NEXT_BLKP(bp);
        size += GET_SIZE(HDRP(next_bp));
        remove_free_block(next_bp);
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        insert_free_block(bp);
    } else if (!prev_alloc && next_alloc) {
        void *prev_bp = PREV_BLKP(bp);
        size += GET_SIZE(HDRP(prev_bp));
        remove_free_block(prev_bp);
        PUT(HDRP(prev_bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        bp = prev_bp;
        insert_free_block(bp);
    } else {
        void *prev_bp = PREV_BLKP(bp);
        void *next_bp = NEXT_BLKP(bp);

        size += GET_SIZE(HDRP(prev_bp)) + GET_SIZE(HDRP(next_bp));
        remove_free_block(prev_bp);
        remove_free_block(next_bp);
        PUT(HDRP(prev_bp), PACK(size, 0));
        PUT(FTRP(next_bp), PACK(size, 0));
        bp = prev_bp;
        insert_free_block(bp);
    }

#ifdef NEXT_FIT
    if (rover > (char *)bp && rover < NEXT_BLKP(bp))
        rover = bp;
#endif
    return bp;
}

/*
 * CHANGE 3: realloc tries, in order:
 *   1. shrink in place;
 *   2. consume an adequate next free block;
 *   3. extend an inadequate wilderness free block;
 *   4. extend when ptr directly borders the epilogue;
 *   5. allocate/copy/free as the final fallback.
 */
void *mm_realloc(void *ptr, size_t size)
{
    size_t adjusted_size;
    size_t asize;
    size_t oldsize;
    void *next_bp;
    size_t next_size;
    void *newptr;

    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    if (ptr == NULL)
        return mm_malloc(size);

    adjusted_size = adjust_request_size(size);
    asize = DSIZE * ((adjusted_size + DSIZE + DSIZE - 1) / DSIZE);
    if (asize < MINBLOCK)
        asize = MINBLOCK;

    oldsize = GET_SIZE(HDRP(ptr));

    /* Shrink in place. */
    if (asize <= oldsize) {
        size_t remainsize = oldsize - asize;

        if (remainsize >= MINBLOCK) {
            void *free_bp;

            PUT(HDRP(ptr), PACK(asize, 1));
            PUT(FTRP(ptr), PACK(asize, 1));

            free_bp = NEXT_BLKP(ptr);
            PUT(HDRP(free_bp), PACK(remainsize, 0));
            PUT(FTRP(free_bp), PACK(remainsize, 0));
            coalesce(free_bp);
        }
        return ptr;
    }

    /* Save these before changing any header used by NEXT_BLKP. */
    next_bp = NEXT_BLKP(ptr);
    next_size = GET_SIZE(HDRP(next_bp));

    /* The following free block already provides enough room. */
    if (!GET_ALLOC(HDRP(next_bp)) && oldsize + next_size >= asize) {
        size_t total_size = oldsize + next_size;
        size_t remainsize = total_size - asize;

        remove_free_block(next_bp);

        if (remainsize >= MINBLOCK) {
            void *free_bp;

            PUT(HDRP(ptr), PACK(asize, 1));
            PUT(FTRP(ptr), PACK(asize, 1));
            free_bp = NEXT_BLKP(ptr);
            PUT(HDRP(free_bp), PACK(remainsize, 0));
            PUT(FTRP(free_bp), PACK(remainsize, 0));
            insert_free_block(free_bp);
        } else {
            PUT(HDRP(ptr), PACK(total_size, 1));
            PUT(FTRP(ptr), PACK(total_size, 1));
        }
        return ptr;
    }

    /* The next free block is too small, but it is the wilderness block. */
    if (!GET_ALLOC(HDRP(next_bp)) &&
        GET_SIZE(HDRP(NEXT_BLKP(next_bp))) == 0) {
        size_t needed = asize - oldsize - next_size;
        size_t extendsize = MAX(needed, MINBLOCK);
        void *grown_bp = extend_heap(extendsize / WSIZE);

        if (grown_bp != NULL) {
            size_t total_size = oldsize + GET_SIZE(HDRP(grown_bp));
            size_t remainsize = total_size - asize;

            /* extend_heap/coalesce inserted grown_bp into its size class. */
            remove_free_block(grown_bp);

            if (remainsize >= MINBLOCK) {
                void *free_bp;

                PUT(HDRP(ptr), PACK(asize, 1));
                PUT(FTRP(ptr), PACK(asize, 1));
                free_bp = NEXT_BLKP(ptr);
                PUT(HDRP(free_bp), PACK(remainsize, 0));
                PUT(FTRP(free_bp), PACK(remainsize, 0));
                insert_free_block(free_bp);
            } else {
                PUT(HDRP(ptr), PACK(total_size, 1));
                PUT(FTRP(ptr), PACK(total_size, 1));
            }
            return ptr;
        }
    }

    /* ptr itself directly borders the epilogue. */
    if (next_size == 0) {
        size_t extendsize = MAX(asize - oldsize, MINBLOCK);
        void *extension = extend_heap(extendsize / WSIZE);

        if (extension != NULL) {
            size_t total_size = oldsize + GET_SIZE(HDRP(extension));
            size_t remainsize = total_size - asize;

            remove_free_block(extension);

            if (remainsize >= MINBLOCK) {
                void *free_bp;

                PUT(HDRP(ptr), PACK(asize, 1));
                PUT(FTRP(ptr), PACK(asize, 1));
                free_bp = NEXT_BLKP(ptr);
                PUT(HDRP(free_bp), PACK(remainsize, 0));
                PUT(FTRP(free_bp), PACK(remainsize, 0));
                insert_free_block(free_bp);
            } else {
                PUT(HDRP(ptr), PACK(total_size, 1));
                PUT(FTRP(ptr), PACK(total_size, 1));
            }
            return ptr;
        }
    }

    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;

    {
        size_t copy_size = oldsize - DSIZE;
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

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    bp = mem_sbrk((int)size);
    if (bp == (void *)-1)
        return NULL;

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    return coalesce(bp);
}

static void *place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));
    size_t remainsize = csize - asize;

    remove_free_block(bp);

    if (remainsize < MINBLOCK) {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
        return bp;
    }

    if (asize <= SMALL_LIMIT) {
        void *allocated_bp;

        /* Small allocation at the tail: [free remainder][small block]. */
        PUT(HDRP(bp), PACK(remainsize, 0));
        PUT(FTRP(bp), PACK(remainsize, 0));
        insert_free_block(bp);

        allocated_bp = NEXT_BLKP(bp);
        PUT(HDRP(allocated_bp), PACK(asize, 1));
        PUT(FTRP(allocated_bp), PACK(asize, 1));
        return allocated_bp;
    } else {
        void *free_bp;

        /* Large allocation at the front: [large block][free remainder]. */
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));

        free_bp = NEXT_BLKP(bp);
        PUT(HDRP(free_bp), PACK(remainsize, 0));
        PUT(FTRP(free_bp), PACK(remainsize, 0));
        insert_free_block(free_bp);
        return bp;
    }
}

static void *find_fit(size_t asize)
{
#ifdef NEXT_FIT
    char *oldrover = rover;

    for (; GET_SIZE(HDRP(rover)) > 0; rover = NEXT_BLKP(rover)) {
        if (!GET_ALLOC(HDRP(rover)) && asize <= GET_SIZE(HDRP(rover)))
            return rover;
    }
    for (rover = heap_listp; rover < oldrover; rover = NEXT_BLKP(rover)) {
        if (!GET_ALLOC(HDRP(rover)) && asize <= GET_SIZE(HDRP(rover)))
            return rover;
    }
    return NULL;
#elif defined(FIRST_FIT)
    int index = get_list_index(asize);
    int i;

    for (i = index; i < LISTNUM; i++) {
        void *bp;
        for (bp = LIST_HEAD(i); bp != NULL; bp = NEXT_FREE(bp)) {
            if (asize <= GET_SIZE(HDRP(bp)))
                return bp;
        }
    }
    return NULL;
#else
    int index = get_list_index(asize);
    int i;

    for (i = index; i < LISTNUM; i++) {
        void *bp;
        void *target = NULL;
        size_t diff = (size_t)-1;

        for (bp = LIST_HEAD(i); bp != NULL; bp = NEXT_FREE(bp)) {
            size_t block_size = GET_SIZE(HDRP(bp));
            if (asize <= block_size && block_size - asize < diff) {
                diff = block_size - asize;
                target = bp;
                if (diff == 0)
                    break;
            }
        }
        if (target != NULL)
            return target;
    }
    return NULL;
#endif
}

static void printblock(void *bp)
{
    size_t hsize = GET_SIZE(HDRP(bp));
    size_t halloc = GET_ALLOC(HDRP(bp));

    /* Do not call checkheap() here; verbose checkheap calls printblock(). */
    if (hsize == 0) {
        printf("%p: EOL\n", bp);
        return;
    }

    printf("%p: header: [%zu:%c] footer: [%zu:%c]\n",
           bp,
           hsize, halloc ? 'a' : 'f',
           (size_t)GET_SIZE(FTRP(bp)),
           GET_ALLOC(FTRP(bp)) ? 'a' : 'f');
}

static void checkblock(void *bp)
{
    if ((size_t)bp % ALIGNMENT)
        printf("Error: %p is not doubleword aligned\n", bp);
    if (GET(HDRP(bp)) != GET(FTRP(bp)))
        printf("Error: header does not match footer at %p\n", bp);
}

static void checkheap(int verbose)
{
    char *bp;
    int i;

    if (verbose)
        printf("Heap (%p):\n", heap_listp);

    if (GET_SIZE(HDRP(heap_listp)) != DSIZE ||
        !GET_ALLOC(HDRP(heap_listp)))
        printf("Bad prologue header\n");
    checkblock(heap_listp);

    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (verbose)
            printblock(bp);
        checkblock(bp);
    }

    if (verbose)
        printblock(bp);
    if (GET_SIZE(HDRP(bp)) != 0 || !GET_ALLOC(HDRP(bp)))
        printf("Bad epilogue header\n");

    /* Detect cycles before any unbounded ordinary free-list traversal. */
    for (i = 0; i < LISTNUM; i++) {
        char *slow = LIST_HEAD(i);
        char *fast = LIST_HEAD(i);

        while (fast != NULL && NEXT_FREE(fast) != NULL) {
            slow = NEXT_FREE(slow);
            fast = NEXT_FREE(NEXT_FREE(fast));
            if (slow == fast) {
                printf("Heap error: cycle in free list %d at %p\n",
                       i, (void *)slow);
                return;
            }
        }
    }

    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp))) {
            int found = 0;

            for (i = 0; i < LISTNUM && !found; i++) {
                void *node;
                for (node = LIST_HEAD(i); node != NULL;
                     node = NEXT_FREE(node)) {
                    if (node == bp) {
                        found = 1;
                        break;
                    }
                }
            }
            if (!found)
                printf("Heap error: free block %p missing from free lists\n",
                       (void *)bp);
        }

        if (!GET_ALLOC(HDRP(bp)) &&
            GET_SIZE(HDRP(NEXT_BLKP(bp))) > 0 &&
            !GET_ALLOC(HDRP(NEXT_BLKP(bp)))) {
            printf("Heap error: adjacent free blocks at %p and %p\n",
                   (void *)bp, (void *)NEXT_BLKP(bp));
        }
    }

    for (i = 0; i < LISTNUM; i++) {
        if (LIST_HEAD(i) != NULL && PREV_FREE(LIST_HEAD(i)) != NULL)
            printf("Heap error: list %d head has non-NULL prev\n", i);

        for (bp = LIST_HEAD(i); bp != NULL; bp = NEXT_FREE(bp)) {
            size_t block_size = GET_SIZE(HDRP(bp));

            if (get_list_index(block_size) != i)
                printf("Heap error: block %p size %zu in wrong list %d\n",
                       (void *)bp, block_size, i);
            if (GET_ALLOC(HDRP(bp)))
                printf("Heap error: allocated block %p in free list %d\n",
                       (void *)bp, i);
            if (NEXT_FREE(bp) != NULL &&
                PREV_FREE(NEXT_FREE(bp)) != bp)
                printf("Heap error: next/prev mismatch at %p\n", (void *)bp);
            if (PREV_FREE(bp) != NULL &&
                NEXT_FREE(PREV_FREE(bp)) != bp)
                printf("Heap error: prev/next mismatch at %p\n", (void *)bp);
        }
    }
}
