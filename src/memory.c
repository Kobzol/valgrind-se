#include "memory.h"

static VA* uniform_va[4];

MemorySpace* current_memspace = NULL;

// VA management
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

// Page management
static INLINE AuxMapEnt* page_find_in_auxmap(Addr base)
{
    AuxMapEnt  key;
    AuxMapEnt* res;
    key.base = base;
    res = VG_(OSetGen_Lookup)(current_memspace->auxmap, &key);
    return res;
}
static INLINE Addr page_get_start(Addr addr)
{
    return (addr & (~PAGE_MASK));
}
static INLINE Bool page_is_start(Addr addr)
{
    return (page_get_start(addr) == addr);
}
static INLINE Addr page_get_offset(Addr addr)
{
    return ((addr) & PAGE_MASK);
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
static Page* page_new_empty(Addr addr)
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
static INLINE Page* page_find(Addr addr)
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
static INLINE void page_set_va(Page* page, VA* va)
{
    //page = page_prepare_for_write_data(page); TODO
    va_dispose(page->va);
    page->va = va;
    va->ref_count++;
}

// memory definedness
static void set_address_range_perms(Addr addr, SizeT length, UChar permission)
{
    // TODO: prepare for VA write?

    UWord start = page_get_start(addr);
    UWord nextPage = start + PAGE_SIZE;
    UWord distanceToNext = nextPage - addr;
    UWord setLength = MIN(distanceToNext, length);
    Page* page = page_find(addr);

    if (setLength == PAGE_SIZE)
    {
        page_set_va(page, uniform_va[permission]);
    }
    else
    {
        Addr offset = page_get_offset(addr);
        VA* va = page->va;
        VG_(memset)(va->vabits + offset, permission, setLength);
    }

    UWord remaining = length - setLength;
    if (remaining > 0)
    {
        set_address_range_perms(addr + setLength, remaining, permission);
    }
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
    tl_assert(heap_space % PAGE_SIZE == 0); // Heap is propertly aligned
    tl_assert(heap_space != 0);

    // initialize uniform VA memory blocks
    uniform_va[MEM_NOACCESS] = va_new();
    VG_(memset)(uniform_va[MEM_NOACCESS]->vabits, MEM_NOACCESS, VA_CHUNKS);
    uniform_va[MEM_UNDEFINED] = va_new();
    VG_(memset)(uniform_va[MEM_UNDEFINED]->vabits, MEM_UNDEFINED, VA_CHUNKS);
    uniform_va[MEM_READONLY] = va_new();
    VG_(memset)(uniform_va[MEM_READONLY]->vabits, MEM_READONLY, VA_CHUNKS);
    uniform_va[MEM_DEFINED] = va_new();
    VG_(memset)(uniform_va[MEM_DEFINED]->vabits, MEM_DEFINED, VA_CHUNKS);

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

void se_handle_new_mmap(Addr a, SizeT len, Bool rr, Bool ww, Bool xx,
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
}