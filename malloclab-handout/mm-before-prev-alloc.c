/* 
 * Simple, 32-bit and 64-bit clean allocator based on implicit free
 * lists, first-fit placement, and boundary tag coalescing, as described
 * in the CS:APP3e text. Blocks must be aligned to doubleword (8 byte) 
 * boundaries. Minimum block size is 16 bytes. 
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};


/*
 * If NEXT_FIT defined use next fit search, else use first-fit search 
 */
#define NEXT_FITx
#define FIRST_FITx

/* $begin mallocmacros */
/* Basic constants and macros */
#define WSIZE       4       /* Word and header/footer size (bytes) */ //line:vm:mm:beginconst
#define DSIZE       8       /* Double word size (bytes) */
#define CHUNKSIZE  (1<<8)  /* Extend heap by this amount (bytes) */  //line:vm:mm:endconst 

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

#define LISTNUM 12

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define MINBLOCK ALIGN(DSIZE + 2 * sizeof(void *))

#define MAX(x, y) ((x) > (y)? (x) : (y))  

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc)) //line:vm:mm:pack

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))            //line:vm:mm:get
#define PUT(p, val)  (*(unsigned int *)(p) = (val))    //line:vm:mm:put

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)                   //line:vm:mm:getsize
#define GET_ALLOC(p) (GET(p) & 0x1)                    //line:vm:mm:getalloc
/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)       ((char *)(bp) - WSIZE)                      //line:vm:mm:hdrp
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) //line:vm:mm:ftrp

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) //line:vm:mm:nextblkp
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) //line:vm:mm:prevblkp

#define NEXT_FREE(bp) (*(void **)(bp))
#define PREV_FREE(bp) (*(void **)((char *)(bp) + sizeof(void *)))

/* $end mallocmacros */

/* Global variables */
static char *heap_listp = 0;  /* Pointer to first block */  
// static char *free_listp = NULL;
static void *free_listp[LISTNUM];

#ifdef NEXT_FIT
static char *rover;           /* Next fit rover */
#endif

#define DEBUG 0  /* 1 开启，0 关闭 */

#if DEBUG
#define CHECKHEAP() checkheap(0)
#else
#define CHECKHEAP() ((void)0)
#endif

/* Function prototypes for internal helper routines */
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

// static void insert_free_block(void *bp) {
//     PREV_FREE(bp) = NULL;
//     NEXT_FREE(bp) = free_listp;
//     if (free_listp) {
//         PREV_FREE(free_listp) = bp;
//     }
    
//     free_listp = bp;
// }

// static void remove_free_block(void *bp) {
//     void* prev = PREV_FREE(bp);
//     void* next = NEXT_FREE(bp);
//     if (prev == NULL) {
//         free_listp = NEXT_FREE(bp);
//     } else {
//         NEXT_FREE(prev) = next;
//     }
//     if (next) {
//         PREV_FREE(next) = prev;
//     }
// }

static int get_list_index(size_t size) {
    int index = 0;
    while (size > MINBLOCK && index < LISTNUM - 1) {
        size = (size + 1) >> 1;
        index++;
    }
    return index;
}

static void insert_free_block(void *bp) {
    int index = get_list_index(GET_SIZE(HDRP(bp)));
    PREV_FREE(bp) = NULL;
    NEXT_FREE(bp) = free_listp[index];
    if (free_listp[index]) {
        PREV_FREE(free_listp[index]) = bp;
    }
    
    free_listp[index] = bp;
}

static void remove_free_block(void *bp) {
     int index = get_list_index(GET_SIZE(HDRP(bp)));
    void* prev = PREV_FREE(bp);
    void* next = NEXT_FREE(bp);
    if (prev == NULL) {
        free_listp[index] = NEXT_FREE(bp);
    } else {
        NEXT_FREE(prev) = next;
    }
    if (next) {
        PREV_FREE(next) = prev;
    }
}
/* 
 * mm_init - Initialize the memory manager 
 */
/* $begin mminit */
int mm_init(void) 
{
    heap_listp = 0;
    for (int i = 0; i < LISTNUM; i++) {
        free_listp[i] = NULL;
    }
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1) //line:vm:mm:begininit
        return -1;
    PUT(heap_listp, 0);                          /* Alignment padding */
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); /* Prologue header */
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); /* Prologue footer */ 
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));     /* Epilogue header */
    heap_listp += (2*WSIZE);                     //line:vm:mm:endinit  
    /* $end mminit */

#ifdef NEXT_FIT
    rover = heap_listp;
#endif
    /* $begin mminit */

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) 
        return -1;
    CHECKHEAP();
    return 0;
}
/* $end mminit */

/* 
 * mm_malloc - Allocate a block with at least size bytes of payload 
 */
/* $begin mmmalloc */
void *mm_malloc(size_t size) 
{
    size_t asize;      /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *bp;      

    /* $end mmmalloc */
    if (heap_listp == 0){
        mm_init();
    }
    /* $begin mmmalloc */
    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= 2 * sizeof(void *))                                          //line:vm:mm:sizeadjust1
        asize = MINBLOCK;                                        //line:vm:mm:sizeadjust2
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE); //line:vm:mm:sizeadjust3

    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {  //line:vm:mm:findfitcall
        bp = place(bp, asize);                  //line:vm:mm:findfitplace
        CHECKHEAP();
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);                 //line:vm:mm:growheap1
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)  
        return NULL;                                  //line:vm:mm:growheap2
    bp = place(bp, asize);                                 //line:vm:mm:growheap3
    CHECKHEAP();
    return bp;
} 
/* $end mmmalloc */

/* 
 * mm_free - Free a block 
 */
/* $begin mmfree */
void mm_free(void *bp)
{
    /* $end mmfree */
    if (bp == 0) 
        return;

    /* $begin mmfree */
    size_t size = GET_SIZE(HDRP(bp));
    /* $end mmfree */
    if (heap_listp == 0){
        mm_init();
    }
    /* $begin mmfree */

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
    CHECKHEAP();
}

/* $end mmfree */
/*
 * coalesce - Boundary tag coalescing. Return ptr to coalesced block
 */
/* $begin mmfree */
static void *coalesce(void *bp) 
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {            /* Case 1 */
        insert_free_block(bp);
        return bp;
    }

    else if (prev_alloc && !next_alloc) {      /* Case 2 */
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        remove_free_block(NEXT_BLKP(bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size,0));
        insert_free_block(bp);
    }

    else if (!prev_alloc && next_alloc) {      /* Case 3 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        remove_free_block(PREV_BLKP(bp));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        insert_free_block(PREV_BLKP(bp));
        bp = PREV_BLKP(bp);
    }

    else {                                     /* Case 4 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + 
            GET_SIZE(FTRP(NEXT_BLKP(bp)));
        remove_free_block(NEXT_BLKP(bp));
        remove_free_block(PREV_BLKP(bp));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        insert_free_block(PREV_BLKP(bp));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    /* $end mmfree */
#ifdef NEXT_FIT
    /* Make sure the rover isn't pointing into the free block */
    /* that we just coalesced */
    if ((rover > (char *)bp) && (rover < NEXT_BLKP(bp))) 
        rover = bp;
#endif
    /* $begin mmfree */
    return bp;
}
/* $end mmfree */

/*
 * mm_realloc - Naive implementation of realloc
 */
void *mm_realloc(void *ptr, size_t size)
{
    size_t oldsize;
    void *newptr;

    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0) {
        mm_free(ptr);
        return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if(ptr == NULL) {
        return mm_malloc(size);
    }

    size_t asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
    if (asize < MINBLOCK) {
        asize = MINBLOCK;
    }
    oldsize = GET_SIZE(HDRP(ptr));
    if (asize <= oldsize) {
        size_t remainsize = oldsize - asize;
        if (remainsize >= MINBLOCK) {
            PUT(HDRP(ptr), PACK(asize, 1));
            PUT(FTRP(ptr), PACK(asize, 1));
            void *fptr = NEXT_BLKP(ptr);
            PUT(HDRP(fptr), PACK(remainsize, 0));
            PUT(FTRP(fptr), PACK(remainsize, 0));
            coalesce(fptr);
        } 
        return ptr;
    } else if (!GET_ALLOC(HDRP(NEXT_BLKP(ptr))) && GET_SIZE(HDRP(NEXT_BLKP(ptr))) >= asize - oldsize) {
        size_t total_size = oldsize + GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        size_t remainsize = total_size - asize;
        remove_free_block(NEXT_BLKP(ptr));
        if (remainsize >= MINBLOCK) {
            PUT(HDRP(ptr), PACK(asize, 1));
            PUT(FTRP(ptr), PACK(asize, 1));
            void *fptr = NEXT_BLKP(ptr);
            PUT(HDRP(fptr), PACK(remainsize, 0));
            PUT(FTRP(fptr), PACK(remainsize, 0));
            insert_free_block(fptr);
        } else {
            PUT(HDRP(ptr), PACK(total_size, 1));
            PUT(FTRP(ptr), PACK(total_size, 1));
        }
        return ptr;
    } else if (GET(HDRP(NEXT_BLKP(ptr))) == 1) {
        size_t extendsize = MAX(asize - oldsize, MINBLOCK);
        void *bp = extend_heap(extendsize/WSIZE);
        if (bp) {
            remove_free_block(bp);
            PUT(HDRP(ptr), PACK(oldsize + extendsize, 1));
            PUT(FTRP(ptr), PACK(oldsize + extendsize, 1));
            return ptr;
        }
    }
    newptr = mm_malloc(size);
    if (!newptr) {
        return 0;
    }
    memcpy(newptr, ptr, oldsize - DSIZE);
    mm_free(ptr);
    return newptr;
}

/* 
 * mm_checkheap - Check the heap for correctness
 */
void mm_checkheap(int verbose)  
{ 
    checkheap(verbose);
}

/* 
 * The remaining routines are internal helper routines 
 */

/* 
 * extend_heap - Extend heap with free block and return its block pointer
 */
/* $begin mmextendheap */
static void *extend_heap(size_t words) 
{
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE; //line:vm:mm:beginextend
    if ((long)(bp = mem_sbrk(size)) == -1)  
        return NULL;                                        //line:vm:mm:endextend

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));         /* Free block header */   //line:vm:mm:freeblockhdr
    PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */   //line:vm:mm:freeblockftr
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */ //line:vm:mm:newepihdr

    /* Coalesce if the previous block was free */
    return coalesce(bp);                                          //line:vm:mm:returnblock
}
/* $end mmextendheap */

/* 
 * place - Place block of asize bytes at start of free block bp 
 *         and split if remainder would be at least minimum block size
 */
/* $begin mmplace */
/* $begin mmplace-proto */
static void* place(void *bp, size_t asize)
/* $end mmplace-proto */
{
    size_t csize = GET_SIZE(HDRP(bp));   

    if ((csize - asize) >= (MINBLOCK)) {
        remove_free_block(bp);
        PUT(HDRP(bp), PACK(csize-asize, 0));
        PUT(FTRP(bp), PACK(csize-asize, 0));
        insert_free_block(bp);
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
    }
    else { 
        remove_free_block(bp);
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
    return bp;
}
/* $end mmplace */

/* 
 * find_fit - Find a fit for a block with asize bytes 
 */
/* $begin mmfirstfit */
/* $begin mmfirstfit-proto */
static void *find_fit(size_t asize)
/* $end mmfirstfit-proto */
{
    /* $end mmfirstfit */

#ifdef NEXT_FIT 
    /* Next fit search */
    char *oldrover = rover;

    /* Search from the rover to the end of list */
    for ( ; GET_SIZE(HDRP(rover)) > 0; rover = NEXT_BLKP(rover))
        if (!GET_ALLOC(HDRP(rover)) && (asize <= GET_SIZE(HDRP(rover))))
            return rover;

    /* search from start of list to old rover */
    for (rover = heap_listp; rover < oldrover; rover = NEXT_BLKP(rover))
        if (!GET_ALLOC(HDRP(rover)) && (asize <= GET_SIZE(HDRP(rover))))
            return rover;

    return NULL;  /* no fit found */
#elif defined(FIRST_FIT) 
    // /* $begin mmfirstfit */
    // /* First-fit search */
    // void *bp;

    // for (bp = free_listp; bp != NULL; bp = NEXT_FREE(bp)) {
    //     if (asize <= GET_SIZE(HDRP(bp))) {
    //         return bp;
    //     }
    // }
    // return NULL; /* No fit */
    void *bp;
    int index = get_list_index(asize);
    for (int i = index; i < LISTNUM; i++) {
        for (bp = free_listp[i]; bp != NULL; bp = NEXT_FREE(bp)) {
            if (asize <= GET_SIZE(HDRP(bp))) {
                return bp;
            }
        }
    }
    
#else 
    /* Best-fit search */
    // void *bp;
    // void *target = NULL;
    // size_t diff = (size_t)-1; 
    // for (bp = free_listp; bp != NULL; bp = NEXT_FREE(bp)) {
    //     if (asize <= GET_SIZE(HDRP(bp)) && GET_SIZE(HDRP(bp)) - asize < diff) {
    //         diff = GET_SIZE(HDRP(bp)) - asize;
    //         target = bp;
    //         if (diff == 0) {
    //             break;
    //         }
    //     }
    // }
    // return target;
    void *bp;
    void *target = NULL;
    size_t diff = (size_t)-1;
    int index = get_list_index(asize); 
    for (int i = index; i < LISTNUM; i++) {
        for (bp = free_listp[i]; bp != NULL; bp = NEXT_FREE(bp)) {
            if (asize <= GET_SIZE(HDRP(bp)) && GET_SIZE(HDRP(bp)) - asize < diff) {
                diff = GET_SIZE(HDRP(bp)) - asize;
                target = bp;
                if (diff == 0) {
                    break;
                }
            }
        }
        if (target != NULL) {
            return target;
        }
    }
    return target;

#endif
}
/* $end mmfirstfit */

static void printblock(void *bp) 
{
    size_t hsize, halloc, fsize, falloc;

    checkheap(0);
    hsize = GET_SIZE(HDRP(bp));
    halloc = GET_ALLOC(HDRP(bp));  
    fsize = GET_SIZE(FTRP(bp));
    falloc = GET_ALLOC(FTRP(bp));  

    if (hsize == 0) {
        printf("%p: EOL\n", bp);
        return;
    }

    printf("%p: header: [%zu:%c] footer: [%zu:%c]\n", bp, 
           hsize, (halloc ? 'a' : 'f'), 
           fsize, (falloc ? 'a' : 'f')); 
}

static void checkblock(void *bp) 
{
    if ((size_t)bp % 8)
        printf("Error: %p is not doubleword aligned\n", bp);
    if (GET(HDRP(bp)) != GET(FTRP(bp)))
        printf("Error: header does not match footer\n");
}

/* 
 * checkheap - Minimal check of the heap for consistency 
 */
void checkheap(int verbose) 
{
    char *bp = heap_listp;

    if (verbose)
        printf("Heap (%p):\n", heap_listp);

    if ((GET_SIZE(HDRP(heap_listp)) != DSIZE) || !GET_ALLOC(HDRP(heap_listp)))
        printf("Bad prologue header\n");
    checkblock(heap_listp);

    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (verbose) 
            printblock(bp);
        checkblock(bp);
    }

    if (verbose)
        printblock(bp);
    if ((GET_SIZE(HDRP(bp)) != 0) || !(GET_ALLOC(HDRP(bp))))
        printf("Bad epilogue header\n");

    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp))) {
            char* bg;
            int found = 0;
            for (int i = 0; i < LISTNUM; i++) {
                for (bg = free_listp[i]; bg != NULL; bg = NEXT_FREE(bg)) {
                    if (bg == bp) {
                        found = 1;
                        break;
                    }
                }
                if (found == 1) {
                    break;
                }
            }
            if (!found) {
                printf("Heap error: free block %p is missing from free list\n", (void *)bp);
            }
            
        }
    }

    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        void *next_bp = NEXT_BLKP(bp);
        if (!GET_ALLOC(HDRP(bp)) &&
        GET_SIZE(HDRP(next_bp)) > 0 &&
        !GET_ALLOC(HDRP(next_bp))) {

        printf("Heap error: adjacent free blocks at %p and %p were not coalesced\n",
               (void *)bp, next_bp);
    }
    }
    
    for (int i = 0; i < LISTNUM; i++) {
        char* slow = free_listp[i];
        char* fast = free_listp[i];
        while (fast != NULL && NEXT_FREE(fast) != NULL) {
            slow = NEXT_FREE(slow);
            fast = NEXT_FREE(NEXT_FREE(fast));
            if (slow == fast) {
                printf("Heap error: cycle detected in free list %d at block %p\n",
                   i, (void *)slow);
            }
        }
    }

    for (int i = 0; i < LISTNUM; i++) {
        if (free_listp[i] != NULL &&
            PREV_FREE(free_listp[i]) != NULL) {
            printf("Heap error: free-list %d head has non-NULL prev pointer\n", i);
        }

        for (bp = free_listp[i];
            bp != NULL;
            bp = NEXT_FREE(bp)) {

            size_t size = GET_SIZE(HDRP(bp));

            if (get_list_index(size) != i) {
                printf("Heap error: free block %p of size %zu is in wrong list %d\n",
                    (void *)bp, size, i);
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

