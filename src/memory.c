#include "memory.h"

VA* uniform_va[4];

MemorySpace* current_memspace = NULL;

/// dump
void dump_alloc_blocks(XArray* blocks)
{
    const char* block_types[3] = {
        "free",
        "used",
        "end"
    };

    Int size = VG_(sizeXA)(blocks);
    for (int i = 0; i < size; i++)
    {
        AllocationBlock* block = VG_(indexXA)(blocks, i);
        PRINT(LOG_DEBUG, "Alloc block: %p, size %lu, %s\n",
              (void*) block->address,
              block->requested_size,
              block_types[block->type]
        );
    }
}
void dump_vabits(VA* va, SizeT start, SizeT count)
{
    for (; start < count; start++)
    {
        PRINT(LOG_DEBUG, "%d", (int) va->vabits[start]);
    }
    PRINT(LOG_DEBUG, "\n");
}
void dump_stacktrace(void)
{
    VG_(get_and_pp_StackTrace)(VG_(get_running_tid)(), 10);
}

/// VA management
static VA* va_new(void)
{
    VA* va = VG_(malloc)("se.va", sizeof(VA));
    va->ref_count = 1;
    return va;
}
static void va_dispose(VA* va)
{
    va->ref_count--;
    if (va->ref_count <= 0)
    {
        tl_assert(va->ref_count == 0);
        VG_(free)(va);
    }
}
VA* va_clone(VA* va)
{
    VA* new_va = va_new();
    VG_(memcpy)(new_va->vabits, va->vabits, sizeof(va->vabits));
    VG_(memcpy)(new_va->sbits, va->sbits, sizeof(va->sbits));
    return new_va;
}

/// page management
static INLINE AuxMapEnt* page_find_in_auxmap(Addr base)
{
    AuxMapEnt  key;
    AuxMapEnt* res;
    key.base = base;
    res = VG_(OSetGen_Lookup)(current_memspace->auxmap, &key);
    return res;
}
static AuxMapEnt* page_find_or_alloc_ent_in_auxmap(Addr a)
{
    AuxMapEnt *nyu, *res;

    a &= ~(Addr) PAGE_MASK;
    res = page_find_in_auxmap(a);
    if (LIKELY(res))
        return res;

    nyu = (AuxMapEnt*) VG_(OSetGen_AllocNode)(
            current_memspace->auxmap, sizeof(AuxMapEnt));
    nyu->base = a;
    nyu->page = NULL;
    VG_(OSetGen_Insert)(current_memspace->auxmap, nyu);
    return nyu;
}
Int are_all_flags_rw(Page *page)
{
    UWord i;
    for (i = 1; i < REAL_PAGES_IN_PAGE; i++)
    {
        if (page->page_flags[i] != PAGEFLAG_RW)
        {
            return False;
        }
    }
    return True;
}

static INLINE Bool page_is_start(Addr addr)
{
    return (page_get_start(addr) == addr);
}
static Page* page_new(Addr addr)
{
    Page* page = (Page*) VG_(malloc)("se.page", sizeof(Page));
    page->base = addr;
    page->ref_count = 1;
    page->data = NULL;
    page->va = NULL;
    return page;
}
Page* page_new_empty(Addr addr)
{
    Page *page = page_new(addr);
    page->va = uniform_va[MEM_NOACCESS];
    page->va->ref_count++;
    if (addr >= current_memspace->heap_space && addr < current_memspace->heap_space_end)
    {
        VG_(memset)(page->page_flags, PAGEFLAG_RW, sizeof(page->page_flags));
    }
    else
    {
        VG_(memset)(page->page_flags, PAGEFLAG_UNMAPPED, sizeof(page->page_flags));

        Addr end = VG_(clstk_end);
        Addr start = end + 1 - STACK_SIZE;
        if (addr >= page_get_start(start) &&
                addr <= page_get_start(end))
        {
            tl_assert(start % VKI_PAGE_SIZE == 0);
            Int i;
            for (i = 0; i < REAL_PAGES_IN_PAGE; i++, addr += VKI_PAGE_SIZE)
            {
                if (addr >= start && addr <= end)
                {
                    page->page_flags[i] = PAGEFLAG_RW;
                }
            }
        }
    }

    return page;
}
Page* page_find(Addr addr)
{
    addr = page_get_start(addr);
    // MemorySpace *ms = current_memspace; TODO: cache

    AuxMapEnt *res = page_find_in_auxmap(addr);

    // no page found, allocate a new one
    if (UNLIKELY(!res))
    {
        res = (AuxMapEnt*) VG_(OSetGen_AllocNode)(current_memspace->auxmap, sizeof(AuxMapEnt));
        res->base = addr;
        res->page = page_new_empty(addr);
        VG_(OSetGen_Insert)(current_memspace->auxmap, res);
        return res->page;
    }

    return res->page;
}
Page* page_find_or_null(Addr addr)
{
    addr = page_get_start(addr);
    // MemorySpace *ms = current_memspace; TODO: cache

    AuxMapEnt *res = page_find_in_auxmap(addr);

    if (UNLIKELY(!res))
    {
        return NULL;
    }

    return res->page;
}
static INLINE void page_set_va(Page* page, VA* va)
{
    page = page_prepare_for_write_data(page);
    va_dispose(page->va);
    page->va = va;
    va->ref_count++;
}
static INLINE void page_update(Page* page)
{
    Addr base = page->base;
    MemorySpace* ms = current_memspace;
    for (UInt i = 0; i < ms->page_cache_size; i++)
    {
        if (ms->page_cache[i]->base == base)
        {
            ms->page_cache[i] = page;
            break;
        }
    }
    AuxMapEnt *res = page_find_or_alloc_ent_in_auxmap(base);
    res->page = page;
}
static Page* page_clone(Page *page)
{
    Page* new_page = page_new(page->base);
    page->va->ref_count++;
    new_page->va = page->va;
    VG_(memcpy)(new_page->page_flags, page->page_flags, sizeof(page->page_flags));
    return new_page;
}
Page* page_prepare_for_write_va(Page* page)
{
    if (UNLIKELY(page->ref_count >= 2))
    {
        page->ref_count--;
        Page* new_page = page_clone(page);
        page->va->ref_count--;
        new_page->va = va_clone(page->va);
        page_update(new_page);
        return new_page;
    }

    if (UNLIKELY(page->va->ref_count >= 2))
    {
        page->va->ref_count--;
        page->va = va_clone(page->va);
        return page;
    }

    return page;
}
Page* page_prepare_for_write_data(Page *page)
{
    if (UNLIKELY(page->ref_count >= 2))
    {
        page->ref_count--;
        Page *new_page = page_clone(page);
        page_update(new_page);
        return new_page;
    }

    return page;
}
UChar* page_get_va(Addr a, Int length, Int* loadedSize)
{
    Page* page = page_find(a);
    Int offset = page_get_offset(a);

    tl_assert(offset + length <= PAGE_SIZE);
    *loadedSize = length;
    return page->va->vabits + offset;
}
void page_dispose(Page *page)
{
    page->ref_count--;
    if (page->ref_count <= 0)
    {
        tl_assert(page->ref_count == 0);
        if (page->data)
        {
            VG_(free)(page->data);
        }
        va_dispose(page->va);
        VG_(free)(page);
    }
}

/// memory definedness
static void set_address_range_perms(Addr a, SizeT lenT, UChar perm)
{
    /*UWord start = page_get_start(a);
    UWord nextPage = start + PAGE_SIZE;
    UWord distanceToNext = nextPage - a;
    UWord setLength = MIN(distanceToNext, lenT);
    Page* page = page_find(a);

    if (setLength == PAGE_SIZE)
    {
        page_set_va(page, uniform_va[perm]);
    }
    else
    {
        page = page_prepare_for_write_va(page);
        Addr offset = page_get_offset(a);
        VA* va = page->va;
        VG_(memset)(va->vabits + offset, perm, setLength);
    }

    UWord remaining = lenT - setLength;
    if (remaining > 0)
    {
        set_address_range_perms(a + setLength, remaining, perm);
    }
    else
    {
        sanity_check_vabits(a, lenT, perm);
    }*/
    Addr origAddr = a;
    VA* va;
    Page *page;
    UWord pg_off;

    Bool resetSymBits = False;
    if (perm == MEM_NOACCESS || perm == MEM_UNDEFINED)
    {
        resetSymBits = True;
    }

    UWord aNext = page_get_start(a) + PAGE_SIZE;
    UWord len_to_next_secmap = aNext - a;
    UWord lenA, lenB;

    // lenT = lenA + lenB (lenA upto first page, lenB is rest)
    if (page_is_start(a)) {
        lenA = 0;
        lenB = lenT;
        goto part2;
    } else if ( lenT <= len_to_next_secmap ) {
        lenA = lenT;
        lenB = 0;
    } else {
        lenA = len_to_next_secmap;
        lenB = lenT - lenA;
    }
    page = page_find(a);
    page = page_prepare_for_write_va(page);
    va = page->va;
    pg_off = PAGE_OFF(a);

    if (resetSymBits)
    {
        while (lenA > 0)
        {
            va->vabits[pg_off] = perm;
            va->sbits[pg_off] = SYM_CONCRETE;
            pg_off++;
            lenA--;
        }
    }
    else
    {
        while (lenA > 0)
        {
            va->vabits[pg_off] = perm;
            pg_off++;
            lenA--;
        }
    }

    a = page_get_start (a) + PAGE_SIZE;

    part2:
    while (lenB >= PAGE_SIZE) {
        page = page_find(a);
        page_set_va(page, uniform_va[perm]);
        lenB -= PAGE_SIZE;
        a += PAGE_SIZE;
    }

    if (lenB == 0) {
        return;
    }

    tl_assert(lenB < PAGE_SIZE);
    page = page_find(a);
    page = page_prepare_for_write_va(page);
    va = page->va;
    pg_off = 0;
    if (resetSymBits)
    {
        while (lenB > 0)
        {
            va->vabits[pg_off] = perm;
            va->sbits[pg_off] = SYM_CONCRETE;
            pg_off++;
            lenB--;
        }
    }
    else
    {
        while (lenB > 0)
        {
            va->vabits[pg_off] = perm;
            pg_off++;
            lenB--;
        }
    }

    sanity_check_vabits(origAddr, lenT, perm);
}
static void set_address_range_page_flags(Addr a, SizeT lenT, UChar value)
{
    tl_assert(a % VKI_PAGE_SIZE == 0);
    if (lenT == 0)
    {
        return;
    }

    Addr origAddr = a;
    SizeT origLen = lenT;

    for(;;)
    {
        Page *page = page_find(a);
        tl_assert(page->ref_count == 1);
        UWord pg_off = page_get_offset(a);

        UInt i;
        for (i = pg_off / VKI_PAGE_SIZE; i < REAL_PAGES_IN_PAGE; i++)
        {
            page->page_flags[i] = value;
            if (lenT <= VKI_PAGE_SIZE)
            {
                sanity_check_page_flags(origAddr, origLen, value);
                return;
            }
            lenT -= VKI_PAGE_SIZE;
        }
        a = page_get_start(a);
        a += PAGE_SIZE;
    }
}
void set_address_range_sym(Addr a, SizeT length, UChar value)
{
    UWord start = page_get_start(a);
    UWord nextPage = start + PAGE_SIZE;
    UWord distanceToNext = nextPage - a;
    UWord setLength = MIN(distanceToNext, length);
    Page* page = page_find(a);

    page = page_prepare_for_write_va(page);
    Addr offset = page_get_offset(a);
    VA* va = page->va;
    VG_(memset)(va->sbits + offset, value, setLength);

    UWord remaining = length - setLength;
    if (remaining > 0)
    {
        set_address_range_perms(a + setLength, remaining, value);
    }
}

static int make_page_flags(Bool rr, Bool ww, Bool xx)
{
    int flags = PAGEFLAG_MAPPED;
    if (rr)
    {
        flags |= PAGEFLAG_READ;
    }
    if (ww)
    {
        flags |= PAGEFLAG_WRITE;
    }
    if (xx)
    {
        flags |= PAGEFLAG_EXECUTE;
    }
    return flags;
}
static INLINE void make_mem_undefined(Addr a, SizeT len)
{
    set_address_range_perms(a, len, MEM_UNDEFINED);
}
static INLINE void make_mem_defined(Addr a, SizeT len)
{
    set_address_range_perms(a, len, MEM_DEFINED);
}
static INLINE void make_mem_readonly(Addr a, SizeT len)
{
    set_address_range_perms(a, len, MEM_READONLY);
}
static INLINE void make_mem_noaccess(Addr a, SizeT len)
{
    set_address_range_perms(a, len, MEM_NOACCESS);
}

/// sanity checks
static void sanity_uniform_vabits(char perm)
{
    for (int i = 0; i < PAGE_SIZE; i++)
    {
        tl_assert(uniform_va[(int)perm]->vabits[i] == perm);
    }
}
void sanity_uniform_vabits_all(void)
{
    sanity_uniform_vabits(MEM_NOACCESS);
    sanity_uniform_vabits(MEM_UNDEFINED);
    sanity_uniform_vabits(MEM_READONLY);
    sanity_uniform_vabits(MEM_DEFINED);
}
void sanity_check_vabits(Addr a, Int len, char perm)
{
    Int remaining = len;
    Page* page = page_find(a);
    Int off = page_get_offset(a);

    while (remaining > 0)
    {
        for (int i = off; (remaining > 0) && (i < PAGE_SIZE); i++)
        {
            remaining--;
            tl_assert(page->va->vabits[i] == perm);
        }

        if (remaining > 0)
        {
            page = page_find(page_get_start(a) + PAGE_SIZE);
            off = 0;
        }
    }
}
void sanity_check_page_flags(Addr a, Int len, char flags)
{
    for(;;)
    {
        Page *page = page_find(a);
        UWord pg_off = page_get_offset(a);

        UInt i;
        for (i = pg_off / VKI_PAGE_SIZE; i < REAL_PAGES_IN_PAGE; i++)
        {
            tl_assert(page->page_flags[i] == flags);
            if (len <= VKI_PAGE_SIZE)
            {
                return;
            }
            len -= VKI_PAGE_SIZE;
        }
        a = page_get_start(a);
        a += PAGE_SIZE;
    }
}

/// memspace
void memspace_init(void)
{
    /* Here we need to reserve an address space for our heap manager,
     * We need deterministic allocator that can be saved into and restored from memimage
     *
     * VG_(malloc) is not good solution because address is taken from a bad memory area and optimization
     * like in memcheck (primary map) cannot be applied
     * But VG_(cli_malloc) is not used because it reports underlying mmap
     * through new_mem_mmap and it causes that the whole heap space would be marked through VA flags.
     * ?? Probably VG_(am_mmap_anon_float_client) should be called
     */

    // initialize heap for user program
    SizeT heap_max_size = HEAP_MAX_SIZE;
    Addr heap_space = (Addr) VG_(arena_memalign)
            (VG_AR_CORE, "se.heap", PAGE_SIZE, heap_max_size);
    tl_assert(heap_space % PAGE_SIZE == 0); // Heap is properly aligned
    tl_assert(heap_space != 0);

    // initialize uniform VA memory blocks
    uniform_va[MEM_NOACCESS] = va_new();
    VG_(memset)(uniform_va[MEM_NOACCESS]->vabits, MEM_NOACCESS, VA_CHUNKS);
    VG_(memset)(uniform_va[MEM_NOACCESS]->sbits, SYM_CONCRETE, VA_CHUNKS);

    uniform_va[MEM_UNDEFINED] = va_new();
    VG_(memset)(uniform_va[MEM_UNDEFINED]->vabits, MEM_UNDEFINED, VA_CHUNKS);
    VG_(memset)(uniform_va[MEM_UNDEFINED]->sbits, SYM_CONCRETE, VA_CHUNKS);

    uniform_va[MEM_READONLY] = va_new();
    VG_(memset)(uniform_va[MEM_READONLY]->vabits, MEM_READONLY, VA_CHUNKS);
    VG_(memset)(uniform_va[MEM_READONLY]->sbits, SYM_CONCRETE, VA_CHUNKS);

    uniform_va[MEM_DEFINED] = va_new();
    VG_(memset)(uniform_va[MEM_DEFINED]->vabits, MEM_DEFINED, VA_CHUNKS);
    VG_(memset)(uniform_va[MEM_DEFINED]->sbits, SYM_CONCRETE, VA_CHUNKS);

    // initialize memspace and allocation blocks
    MemorySpace *ms = VG_(malloc)("se.memspace", sizeof(MemorySpace));
    VG_(memset)(ms, 0, sizeof(MemorySpace));
    ms->auxmap = VG_(OSetGen_Create)(offsetof(AuxMapEnt, base), NULL, VG_(malloc), "se.auxmap", VG_(free));
    ms->heap_space = heap_space;
    ms->heap_space_end = heap_space + heap_max_size;
    ms->allocation_blocks = VG_(newXA) (VG_(malloc), "se.allocations", VG_(free), sizeof(AllocationBlock));
    AllocationBlock block;
    block.address = heap_space;
    block.type = BLOCK_END;
    VG_(addToXA)(ms->allocation_blocks, &block);

    tl_assert(current_memspace == NULL);
    current_memspace = ms;
}
static
Addr memspace_alloc(SizeT alloc_size, SizeT allign)
{
    SizeT requested_size = alloc_size;
    alloc_size += REDZONE_SIZE;
    if (UNLIKELY(alloc_size == 0))
    {
        alloc_size = 1;
    }
    // align size
    alloc_size = (((alloc_size - 1) / sizeof(void*)) + 1) * sizeof(void*);

    // find next empty block
    XArray *a = current_memspace->allocation_blocks;
    Word i = 0;
    AllocationBlock *block = VG_(indexXA)(a, 0);
    AllocationBlock *next;
    while (block->type != BLOCK_END)
    {
        next = VG_(indexXA)(a, i + 1);
        if (block->type == BLOCK_FREE)
        {
            Word s = next->address - block->address;
            if (s >= alloc_size)
            {
                Addr old_address = block->address;
                Addr address = ((old_address - 1) / allign + 1) * allign;
                SizeT padding = address - block->address;
                if (s >= alloc_size + padding)
                {
                    block->type = BLOCK_USED;
                    block->requested_size = requested_size;
                    if (padding > 0)
                    {
                        block->address = address;
                        AllocationBlock new_block;
                        new_block.type = BLOCK_FREE;
                        new_block.address = old_address;
                        VG_(insertIndexXA)(a, i, &new_block);
                        i++;
                    }
                    SizeT diff = s - (alloc_size + padding);
                    if (diff > 0)
                    {
                        AllocationBlock new_block;
                        new_block.type = BLOCK_FREE;
                        new_block.address = address + alloc_size;
                        VG_(insertIndexXA)(a, i + 1, &new_block);
                    }
                    return address;
                }
            }
        }
        block = next;
        i++;
    }

    // No sufficient block found, create a new one
    Addr old_address = block->address;
    Addr address = ((old_address - 1) / allign + 1) * allign;
    SizeT padding = address - block->address;

    if (padding > 0)
    {
        block->type = BLOCK_FREE;
        AllocationBlock new_block;
        new_block.type = BLOCK_USED;
        new_block.address = address;
        new_block.requested_size = requested_size;
        VG_(addToXA)(a, &new_block);
    }
    else
    {
        block->type = BLOCK_USED;
        block->requested_size = requested_size;
    }

    AllocationBlock new_block;
    new_block.type = BLOCK_END;
    new_block.requested_size = requested_size;
    new_block.address = address + alloc_size;

    if (UNLIKELY(new_block.address - \
                current_memspace->heap_space >= HEAP_MAX_SIZE))
    {
        PRINT(LOG_ERROR, "HEAP ERROR");
        tl_assert(0);
    }

    VG_(addToXA)(a, &new_block);
    return address;
}
static SizeT memspace_block_size(Addr address)
{
    XArray *a = current_memspace->allocation_blocks;
    Word i, s = VG_(sizeXA)(a);
    AllocationBlock *block = NULL, *next;
    for (i = 0; i < s; i++)
    {
        block = VG_(indexXA)(a, i);
        if (block->address == address)
        {
            break;
        }
    }
    tl_assert(block && block->type == BLOCK_USED);
    next = VG_(indexXA)(a, i + 1);
    SizeT size = next->address - address;
    return size;
}
static
SizeT memspace_free(Addr address)
{
    // TODO: replace by bisect search, array is sorted by address
    XArray *a = current_memspace->allocation_blocks;
    Word i, s = VG_(sizeXA)(a);
    AllocationBlock *block = NULL, *next, *prev;
    for (i = 0; i < s; i++)
    {
        block = VG_(indexXA)(a, i);
        if (block->address == address)
        {
            break;
        }
    }
    tl_assert(block && block->type == BLOCK_USED);
    block->type = BLOCK_FREE;
    next = VG_(indexXA)(a, i + 1);
    SizeT size = next->address - address;

    // TODO: change address/size of the freed block?
    if (next->type == BLOCK_FREE)
    {
        VG_(removeIndexXA)(a, i + 1);
    }
    if (i > 0)
    {
        prev = VG_(indexXA)(a, i - 1);
        if (prev->type == BLOCK_FREE)
        {
            VG_(removeIndexXA)(a, i);
        }
    }
    return size;
}

/// mmap
void se_handle_mmap(Addr a, SizeT len, Bool rr, Bool ww, Bool xx,
                        ULong di_handle)
{
    if (rr && ww)
    {
        make_mem_defined(a, len);
    }
    else if (rr)
    {
        make_mem_readonly(a, len);
    }
    else
    {
        make_mem_noaccess(a, len);
    }

    set_address_range_page_flags(a, len, make_page_flags(rr, ww, xx));
}
void se_handle_mstartup(Addr a, SizeT len, Bool rr, Bool ww, Bool xx,
                              ULong di_handle)
{
    if (rr && ww)
    {
        make_mem_defined(a, len);
    }
    else if (rr)
    {
        make_mem_readonly(a, len);
    }
    else
    {
        make_mem_noaccess(a, len);
    }

    Addr offset = a % VKI_PAGE_SIZE;
    set_address_range_page_flags(a - offset, len + offset, make_page_flags(rr, ww, xx));
}
void se_handle_mprotect(Addr a, SizeT len, Bool rr, Bool ww, Bool xx)
{
    if (rr && ww)
    {
        make_mem_undefined(a, len);
    }
    else if (rr)
    {
        make_mem_readonly(a, len);
    }
    else
    {
        make_mem_noaccess(a, len);
    }

    set_address_range_page_flags(a, len, make_page_flags(rr, ww, xx));
}
void se_handle_munmap(Addr a, SizeT len)
{
    make_mem_noaccess(a, len);
    set_address_range_page_flags(a, len, PAGEFLAG_UNMAPPED);
}
void se_handle_mremap(Addr src, Addr dst, SizeT len)
{
    PRINT(LOG_ERROR, "remap not implemented yet");
    tl_assert(0);
}

/// stack alloc
void se_handle_stack_signal(Addr a, SizeT len, ThreadId tid)
{
    VG_(memset)((void*) (a - VG_STACK_REDZONE_SZB), 0, len);
    make_mem_undefined(a - VG_STACK_REDZONE_SZB, len);
}
void se_handle_stack_new(Addr a, SizeT len)
{
    make_mem_undefined(a - VG_STACK_REDZONE_SZB, len);
}
void se_handle_stack_die(Addr a, SizeT len)
{
    make_mem_noaccess(a - VG_STACK_REDZONE_SZB, len);
}
void se_handle_stack_ban(Addr a, SizeT len)
{
    make_mem_noaccess(a - VG_STACK_REDZONE_SZB, len);
}

void se_handle_post_mem_write(CorePart part, ThreadId tid, Addr a, SizeT len)
{
    make_mem_defined(a, len);
}

/// user malloc
void* se_handle_malloc(ThreadId tid, SizeT n)
{
    Addr addr = memspace_alloc(n, 1);
    VG_(memset)((void*) addr, 0, n);
    make_mem_undefined(addr, n);

    PRINT(LOG_DEBUG, "Malloc %p, size %lu\n", (void*) addr, n);

    return (void*) addr;
}
void* se_handle_memalign(ThreadId tid, SizeT alignB, SizeT n)
{
    Addr addr = memspace_alloc(n, alignB);
    VG_(memset)((void*) addr, 0, n);
    make_mem_undefined(addr, n);
    return (void*) addr;
}
void* se_handle_calloc(ThreadId tid, SizeT nmemb, SizeT size1)
{
    SizeT size = nmemb * size1;
    Addr addr = memspace_alloc(size, 1);
    VG_(memset)((void*)addr, 0, size);
    make_mem_defined(addr, size);
    return (void*) addr;
}
void* se_handle_realloc(ThreadId tid, void* p_old, SizeT new_szB)
{
    Addr addr = memspace_alloc(new_szB, 1);
    // TODO: We should properly copy VA values from the original block
    make_mem_defined(addr, new_szB);

    if (p_old == NULL)
    {
        VG_(memset)((void*)addr, 0, new_szB);
        return (void*) addr;
    }
    SizeT s = memspace_block_size((Addr) p_old);
    tl_assert(s <= new_szB && "realloc size must be greater than the old size");
    VG_(memcpy)((void*) addr, p_old, s);
    if (new_szB > s)
    {
        VG_(memset)((void*)(addr + s), 0, new_szB - s);
    }
    return (void*) addr;
}
SizeT se_handle_malloc_usable_size(ThreadId tid, void* p)
{
    VG_(tool_panic)("client_malloc_usable_size: Not implemented");
}
void se_handle_free(ThreadId tid, void *a)
{
    SizeT size = memspace_free((Addr) a);
    make_mem_noaccess((Addr) a, size);
}
