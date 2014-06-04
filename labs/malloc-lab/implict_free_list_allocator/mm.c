/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "mm.h"
#include "memlib.h"

#define LOG_DEBUG_STR
#ifdef LOG_DEBUG_STR
 #define DebugStr(args...) fprintf(stderr, args);
#else
 #define DebugStr(args...)
#endif

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "xrandom",
    /* First member's full name */
    "xrandom",
    /* First member's email address */
    "xrandom@ab.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

// #ifdef __x86_64__
// #warning "64"
// #else
// #warning "32"
// #endif

/* single word (4) or double word (8) alignment */
#define ALIGNMENT      8
#define MIN_BK_SIZE    8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)
#define IS_ALIGN(size)      (!(size & (ALIGNMENT - 1)))

#define IS_ALIGN_WITH_MIN_BK_SIZE(size)        (!(size & (MIN_BK_SIZE-1)))

// if complie with -m32, sizeof(size_t) = 4
// if complie with -m64, sizeof(size_t) = 8
// but in either case, ALIGN(sizeof(size_t)) = 8
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define WSIZE                           4
#define DSIZE                           8
#define CHUNKSIZE                       (1 << 12)               // equal the page size 4kb
// get multiple of CHUNKSIZE
#define ALIGN_CHUNKSIZE(size)           (((size) + (CHUNKSIZE - 1)) & ~(CHUNKSIZE-1));

#define READ_WORD(p)                                (*(uint32_t*)(p))
#define WRITE_WORD(p, val)                          (*(uint32_t*)(p) = (val))
// Macro Operations
// Allocated Memory Structure
// [ 1 W Header | Payload | [Optional Padding] | 1 W Footer]
// hdrp (header pointer) points to the start address of header
// ftrp (footer pointer) points to the start address of footer
// pldp (payload pointer) points to the start address of payload

#define CURR_ALLOC                                  (1 << 0)
#define PREV_ALLOC                                  (1 << 1)
#define SET_CURR_ALLOC_BIT(p)                       (WRITE_WORD(p, READ_WORD(p) | CURR_ALLOC))
#define CLR_CURR_ALLOC_BIT(p)                       (WRITE_WORD(p, READ_WORD(p) & ~CURR_ALLOC))
#define SET_PREV_ALLOC_BIT(p)                       (WRITE_WORD(p, READ_WORD(p) | PREV_ALLOC))
#define CLR_PREV_ALLOC_BIT(p)                       (WRITE_WORD(p, READ_WORD(p) & ~PREV_ALLOC))

#define PACK(size, alloc)                           ((size) | (alloc))
#define SIZE_MASK                                   (~(ALIGNMENT - 1))
#define SET_SIZE(hdrp, size)                        (WRITE_WORD(hdrp, (READ_WORD(hdrp) & (ALIGNMENT-1)) | size))
#define GET_SIZE(hdrp)                              (READ_WORD(hdrp) & SIZE_MASK)  // get block size from hdrp
#define GET_ALLOC(hdrp)                             (READ_WORD(hdrp) & 0x1)  // get alloc bit from hdrp
#define GET_PREV_ALLOC(hdrp)                        (READ_WORD(hdrp) & 0x2)  // get alloc bit of previous block
#define HDRP_USE_PLDP(pldp)                         ((char*)(pldp) - WSIZE)  // get hdrp using pldp
#define FTRP_USE_PLDP(pldp)                         ((char*)(pldp) + GET_SIZE(HDRP_USE_PLDP(pldp)) - DSIZE)  // get ftrp using pldp

#define PREV_FTRP_USE_PLDP(pldp)                    ((char*)HDRP_USE_PLDP(pldp) - WSIZE)  // get previous block footer using current pldp
#define NEXT_HDRP_USE_PLDP(pldp)                    (HDRP_USE_PLDP((char*)(pldp) + GET_SIZE(HDRP_USE_PLDP(pldp))))  // get the next block header using current pldp
#define PREV_HDRP_USE_PLDP(pldp)                    (HDRP_USE_PLDP((char*)(pldp) - GET_SIZE(PREV_FTRP_USE_PLDP(pldp))))) // get the header of previous using current pldp

#define NEXT_PLDP_USE_PLDP(pldp)                    ((char*)(pldp) + GET_SIZE(HDRP_USE_PLDP(pldp)))  // get next block pldp using current pldp
#define PREV_PLDP_USE_PLDP(pldp)                    ((char*)(pldp) - GET_SIZE(PREV_FTRP_USE_PLDP(pldp)))  // get previous block pldp using current pldp
#define NEXT_PLDP_USE_HDRP(hdrp)                    ((char*)(hdrp) + GET_SIZE(hdrp) + WSIZE)  // get the next pldp using current hdrp

#define NEXT_HDRP_USE_HDRP(hdrp)                    ((char*)(hdrp) + GET_SIZE(hdrp))

// Function Declaration
void pointer_macro_test(void);
static void *extend_heap(size_t size);
static void *coalesce(void *pldp);
// the input size should be aligned
static void *find_first_fit(size_t asize);
// the input size should be aligned
static void place_and_split(void *pldp, size_t asize);

// For Debug
#define __HEAP_CHECK__
#ifdef __HEAP_CHECK__
static void* heap_head = NULL; // points to heap start
static void* heap_tail = NULL; // points to heap end, as Epilogue header
#endif

static void* heap_listp = NULL;
/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void) {
  // Why 4 * WSIZE
  // 1 WSIZE for 0 padding
  // 2 WSIZE for Prologue block
  // 1 WSIZE for Epllogue block
  if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void*)(-1)) {
    return -1;
  }

  #ifdef __HEAP_CHECK__
  heap_head = heap_listp;
  heap_tail = heap_listp + 3 * WSIZE;
  #endif

  WRITE_WORD(heap_listp, 0); // 1 WSIZE for 0 padding
  WRITE_WORD(heap_listp + WSIZE, PACK(DSIZE, PREV_ALLOC | CURR_ALLOC));  // Prologue header
  WRITE_WORD(heap_listp + 2 * WSIZE, PACK(DSIZE, PREV_ALLOC | CURR_ALLOC)); // Prologue footer
  WRITE_WORD(heap_listp + 3 * WSIZE, PACK(0, PREV_ALLOC | CURR_ALLOC)); // Epilogue header
  heap_listp += 3 * WSIZE;


  void* pldp = extend_heap(CHUNKSIZE);
  if (pldp == NULL) return -1;
  return 0;
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size) {
  // Ignore spurious requests
  if (!size) return NULL;
  size_t asize = ALIGN(size + WSIZE);
  DebugStr("mm_malloc with size: %u\n", asize);
  void *pldp = find_first_fit(asize);
  if (pldp) {
    place_and_split(pldp, asize);
    return pldp;
  }

  if (!(pldp = extend_heap(asize))) {
    return NULL;
  }

  place_and_split(pldp, asize);
  return pldp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *pldp) {
  void *hdrp = (char*)pldp - WSIZE;
  size_t size = GET_SIZE(hdrp);
  DebugStr("mm_free with size: %u\n", size);
  CLR_CURR_ALLOC_BIT(hdrp);
  WRITE_WORD((char*)hdrp + size - WSIZE, READ_WORD(hdrp)); // footer is same as header
  CLR_PREV_ALLOC_BIT((char*)hdrp + size); // clear the prev_alloc bit of next block, since current block is free
  coalesce(pldp); // immediate coalescing
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size) {
  void *oldptr = ptr;
  void *newptr;
  size_t copySize;

  newptr = mm_malloc(size);
  if (newptr == NULL) return NULL;
  copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
  if (size < copySize) copySize = size;
  memcpy(newptr, oldptr, copySize);
  mm_free(oldptr);
  return newptr;
}

static void *extend_heap(size_t size) {
  DebugStr("extend heap...\n");
  char *hdrp;
  size_t asize = ALIGN_CHUNKSIZE(size);
  if ((void*)(hdrp = mem_sbrk(asize)) == (void*)(-1)) return NULL;
  hdrp = (char*)hdrp - WSIZE; // points to the old Epliogue header
  WRITE_WORD(hdrp, PACK(asize, GET_PREV_ALLOC(hdrp))); // set new header
  WRITE_WORD((char*)hdrp + asize - WSIZE, READ_WORD(hdrp)); // footer is same as header
  WRITE_WORD(hdrp + asize, PACK(0, CURR_ALLOC)); // write new epilogue block
  return coalesce((char*)hdrp + WSIZE);
}

static void *coalesce(void *pldp) {
  void *hdrp = HDRP_USE_PLDP(pldp);
  size_t size = GET_SIZE(hdrp);
  size_t prev_alloc = GET_PREV_ALLOC(hdrp); // read current header to get the privous block allocate bit
  size_t next_alloc = GET_ALLOC((char*)hdrp + size); // read next block header to get its allocate bit

  // previous and next are both allocated
  if (prev_alloc && next_alloc) {
    return pldp;
  }

  // previous is free but next is allocated
  if (!prev_alloc && next_alloc) {
    void *prev_hdrp = (char*)hdrp - GET_SIZE(hdrp - WSIZE); // get previous block header pointer
    WRITE_WORD(prev_hdrp, READ_WORD(prev_hdrp) + size); // write new header in previous block
    WRITE_WORD((char*)hdrp + size - WSIZE, READ_WORD(prev_hdrp)); // write new footer in current block
    return (char*)prev_hdrp + WSIZE;
  }

  // next is free but previous is allocated
  if (prev_alloc && !next_alloc) {
    void *next_hdrp = (char*)hdrp + size;
    size_t next_size = GET_SIZE(next_hdrp);
    WRITE_WORD(hdrp, READ_WORD(hdrp) + next_size); // write new header in current block
    WRITE_WORD((char*)next_hdrp + next_size, READ_WORD(hdrp)); // write new footer in next block
    return pldp;
  }

  // previous and next are both free
  size_t prev_size = GET_SIZE((char*)hdrp - WSIZE);
  void *next_hdrp = (char*)hdrp + size;
  void *prev_hdrp = (char*)hdrp - prev_size;
  size_t next_size = GET_SIZE(next_hdrp);
  WRITE_WORD(prev_hdrp, READ_WORD(prev_hdrp) + size + next_size); // write new header
  WRITE_WORD((char*)next_hdrp + next_size - WSIZE, READ_WORD(prev_hdrp));
  return (char*)prev_hdrp + WSIZE;
}

// linear search
static void *find_first_fit(size_t asize) {
  DebugStr("find_first_fit...");
  void *hdrp = heap_listp;
  size_t b_size;
  do {
    b_size = GET_SIZE(hdrp);
    DebugStr("b_size = %u...", b_size);
    if (asize <= b_size && !GET_ALLOC(hdrp)) {
      DebugStr("Enough...\n");
      return (char*)hdrp + WSIZE;
    }
    hdrp = (char*)hdrp + b_size;
  } while (b_size > 0);
  DebugStr("\n");
  return NULL;
}

static void place_and_split(void *pldp, size_t asize) {
  void *hdrp = HDRP_USE_PLDP(pldp);
  size_t b_size = GET_SIZE(hdrp);
  size_t left_size = b_size - asize;
  DebugStr("left size %u...", left_size);
  // split
  if (left_size && IS_ALIGN_WITH_MIN_BK_SIZE(left_size)) { // reminder > 0 and is multiple of minimum blockasize
    DebugStr("split...\n");
    void *new_free_hdrp = (char*)hdrp + asize;
    WRITE_WORD(new_free_hdrp, PACK(left_size, 0));
    WRITE_WORD((char*)new_free_hdrp + left_size - WSIZE, READ_WORD(new_free_hdrp));
    b_size = asize;
  }
  SET_SIZE(hdrp, b_size);
  SET_CURR_ALLOC_BIT(hdrp);
  WRITE_WORD((char*)hdrp + b_size - WSIZE, READ_WORD(hdrp));
  SET_PREV_ALLOC_BIT((char*)hdrp + b_size);
}

void pointer_macro_test() {
  assert(CURR_ALLOC == 0x1);
  assert(PREV_ALLOC == 0x2);
  assert(SIZE_MASK == 0xFFFFFFF8);
}

void ToBinaryStr(size_t num, int sep) {
  int size = sizeof(num) * 8;
  int i;
  for (i = size - 1; i >= 0; --i) {
    fprintf(stderr, "%d", !!(num & (1 << i)));
    if (sep && !(i % 4)) {
      fprintf(stderr, " ");
    }
  }
  fprintf(stderr, "\n");
}

void ToHexStr(size_t num, int sep) {
  static size_t mask = (1 << 4) - 1;
  int size = sizeof(num) * 8;
  int i;
  for (i = size - 4; i >= 0; i -= 4) {
    fprintf(stderr, "%x", (num & (mask << i)) >> i);
    if (sep && !(i % 8)) {
      fprintf(stderr, " ");
    }
  }
  fprintf(stderr, "\n");
}