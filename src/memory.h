#ifndef VALGRIND_MEMORY_H
#define VALGRIND_MEMORY_H

#include "common.h"
#include "pub_tool_stacktrace.h"

/// Defines
#define MEMSPACE_PAGE_CACHE_SIZE 12

#define STACK_SIZE (8 * 1024 * 1024)
#define HEAP_MAX_SIZE (512 * 1024 * 1024)
#define REDZONE_SIZE (16)

#define PAGE_SIZE 65536
#define PAGE_MASK (PAGE_SIZE-1)                        /* DO NOT CHANGE */
#define REAL_PAGES_IN_PAGE (PAGE_SIZE / VKI_PAGE_SIZE) /* DO NOT CHANGE */

#define VA_CHUNKS 65536
#define PAGE_OFF(a) ((a) & PAGE_MASK)

#define MEM_NOACCESS  0 // 00
#define MEM_UNDEFINED 1 // 01
#define MEM_READONLY  2 // 10
#define MEM_DEFINED   3 // 11

#define MEM_READ_MASK  2 // 10
#define MEM_WRITE_MASK 1 // 01

#define PAGEFLAG_UNMAPPED 0 // 0000
#define PAGEFLAG_MAPPED   1 // 0001
#define PAGEFLAG_READ     2 // 0010
#define PAGEFLAG_WRITE    4 // 0100
#define PAGEFLAG_EXECUTE  8 // 1000
#define PAGEFLAG_RW (PAGEFLAG_MAPPED | PAGEFLAG_READ | PAGEFLAG_WRITE)

#define REG_REAL 0
#define REG_SHADOW 1

#define SYM_CONCRETE 0
#define SYM_SYMBOLIC 1

/// Structs
typedef struct {
    Int ref_count;
    UChar vabits[VA_CHUNKS];
    UChar sbits[VA_CHUNKS];
} VA;

typedef struct {
    Addr base;
    Int ref_count;
    UChar page_flags[REAL_PAGES_IN_PAGE];
    VA* va;
    UChar* data;
} Page;

typedef struct {
    Addr    base;
    Page* page;
} AuxMapEnt;

typedef struct {
    Int page_cache_size;
    Page* page_cache[MEMSPACE_PAGE_CACHE_SIZE];
    OSet* auxmap;
    OSet* symmap;
    Addr heap_space;
    Addr heap_space_end;
    XArray* allocation_blocks;
} MemorySpace;

typedef enum {
    BLOCK_FREE,
    BLOCK_USED,
    BLOCK_END
} AllocationBlockType;

typedef struct {
    Addr address;
    AllocationBlockType type;
    SizeT requested_size;
} AllocationBlock;

/// Global variables
extern MemorySpace* current_memspace;


/// init
void memspace_init(void);

/// dump
void dump_alloc_blocks(XArray* blocks);
void dump_vabits(VA* va, SizeT start, SizeT count);
void dump_bits(UChar* bits, SizeT count);
void dump_stacktrace(void);

/// VA
extern VA* uniform_va[4];
VA* va_clone(VA* va);

/// page
Page* page_find(Addr addr);
Page* page_find_or_null(Addr addr);
INLINE Addr page_get_start(Addr addr)
{
    return (addr & (~PAGE_MASK));
}
INLINE Addr page_get_offset(Addr addr)
{
    return ((addr) & PAGE_MASK);
}
Page* page_prepare_for_write_data(Page* page);
Page* page_prepare_for_write_va(Page* page);
UChar* page_get_va(Addr a, Int length, Int* loadedSize);
Page* page_new_empty(Addr addr);
void page_dispose(Page* page);
Int are_all_flags_rw(Page* page);

// heap
Bool mem_is_heap(Addr a);

// sym
void set_address_range_sym(Addr a, SizeT length, UChar value);

/// sanity checks
void sanity_check_vabits(Addr a, Int len, char perm);
void sanity_check_page_flags(Addr a, Int len, char flags);
void sanity_uniform_vabits_all(void);

/// mmap
void se_handle_mmap(Addr a, SizeT len, Bool rr, Bool ww, Bool xx,
                        ULong di_handle);
void se_handle_mstartup(Addr a, SizeT len, Bool rr, Bool ww, Bool xx,
                        ULong di_handle);
void se_handle_mprotect(Addr a, SizeT len, Bool rr, Bool ww, Bool xx);
void se_handle_munmap(Addr a, SizeT len);
void se_handle_mremap(Addr src, Addr dst, SizeT len);

/// stack alloc
void se_handle_stack_signal(Addr a, SizeT len, ThreadId tid);
void se_handle_stack_new(Addr a, SizeT len);
void se_handle_stack_die(Addr a, SizeT len);
void se_handle_stack_ban(Addr a, SizeT len);

void se_handle_post_mem_write(CorePart part, ThreadId tid, Addr a, SizeT len);

/// user malloc
void* se_handle_malloc(ThreadId tid, SizeT n);
void* se_handle_memalign(ThreadId tid, SizeT alignB, SizeT n);
void* se_handle_calloc(ThreadId tid, SizeT nmemb, SizeT size1);
void* se_handle_realloc(ThreadId tid, void* p_old, SizeT new_szB);
SizeT se_handle_malloc_usable_size(ThreadId tid, void* p);
void se_handle_free(ThreadId tid, void *a);

#endif //VALGRIND_MEMORY_H
