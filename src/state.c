#include "state.h"


static INLINE UWord make_new_id(void)
{
    static UWord unique_id_counter = 200;
    return unique_id_counter++;
}
static int flags_to_mmap_protection(int flags)
{
    int prot = 0;
    if (flags & PAGEFLAG_READ)
    {
        prot |= VKI_PROT_READ;
    }

    if (flags & PAGEFLAG_WRITE)
    {
        prot |= VKI_PROT_WRITE;
    }

    if (flags & PAGEFLAG_EXECUTE)
    {
        prot |= VKI_PROT_EXEC;
    }

    return prot;
}

static void memimage_save_page_content(Page* page)
{
    UWord i, j;
    VA* va = page->va;

    // no access data, no need to copy
    if (va == uniform_va[MEM_NOACCESS])
    {
        return;
    }

    // allocate data buffer
    if (page->data == NULL)
    {
        page->data = VG_(malloc)("se.page.data", PAGE_SIZE);
    }

    UChar* src = (UChar*) page->base;
    UChar* dst = page->data;

    // data is defined or read only, we can simply copy it
    if (va == uniform_va[MEM_DEFINED] || va == uniform_va[MEM_READONLY])
    {
        VG_(memcpy)(dst, src, PAGE_SIZE);
        return;
    }

    // we have to copy only the readable bytes one by one
    for (i = 0; i < REAL_PAGES_IN_PAGE; i++)
    {
        if (page->page_flags[i] & PAGEFLAG_READ)
        {
            for (j = VKI_PAGE_SIZE * i; j < VKI_PAGE_SIZE * (i + 1); j++)
            {
                if (va->vabits[j] & MEM_READ_MASK)
                {
                    dst[j] = src[j];
                }
            }
        }
    }
}
static void memimage_save_symmap(MemoryImage* memimage)
{
    UWord size = VG_(OSetGen_Size)(current_memspace->symmap);
    memimage->sym_entries = VG_(malloc)("se.symmap", size * sizeof(SymConMapEnt));
    memimage->sym_entries_count = size;

    VG_(OSetGen_ResetIter)(current_memspace->symmap);
    SymConMapEnt* elem;
    SymConMapEnt* stateElem = memimage->sym_entries;
    while ((elem = VG_(OSetGen_Next(current_memspace->symmap))))
    {
        *stateElem++ = *elem;
    }
}
static void memimage_save(MemoryImage* memimage)
{
    // count the number of pages
    Word size = VG_(OSetGen_Size)(current_memspace->auxmap);
    memimage->pages_count = size;
    Page **pages = (Page**) VG_(malloc)("se.memimage", size * sizeof(Page*));
    memimage->pages = pages;

    // traverse all pages
    VG_(OSetGen_ResetIter)(current_memspace->auxmap);
    AuxMapEnt* elem;
    while ((elem = VG_(OSetGen_Next(current_memspace->auxmap))))
    {
        *pages++ = elem->page;
        // TODO: ask
        if (elem->page->ref_count++ < 2)
        {
            memimage_save_page_content(elem->page);
        }
    }

    memimage_save_symmap(memimage);

    memimage->allocation_blocks = VG_(cloneXA)("se.allocation_blocks",
                                               current_memspace->allocation_blocks);
}

static void memimage_restore_page_content(Page* page, Page* old_page)
{
    tl_assert(page->base == old_page->base);

    if (page->va == uniform_va[MEM_NOACCESS] || page->va == uniform_va[MEM_UNDEFINED])
    {
        return;
    }

    UWord i, j;
    UChar *dst = (UChar*) page->base;
    VA *va = page->va;
    UChar *src = page->data;
    tl_assert(src);

    if (va == uniform_va[MEM_DEFINED])
    {
        if (are_all_flags_rw(page))
        {
            VG_(memcpy)(dst, src, PAGE_SIZE);
            return;
        }
    }
    for (i = 0; i < REAL_PAGES_IN_PAGE; i++)
    {
        int flags = page->page_flags[i];
        int old_flags = old_page->page_flags[i];

        if (flags != old_flags)
        {
            Addr base = page->base + VKI_PAGE_SIZE * i;
            /* The following code handles only the situation when
             old page is unmapped and new flag is RW,
             other situation needs to be handled separately
          */
            if (flags == PAGEFLAG_UNMAPPED)
            {
                SysRes sr = ML_(am_do_munmap_NO_NOTIFY)(base, VKI_PAGE_SIZE);
                tl_assert(!sr_isError(sr));
                VG_(am_notify_munmap)(base, VKI_PAGE_SIZE);
                continue;
            }

            tl_assert(old_flags == PAGEFLAG_UNMAPPED);
            tl_assert(flags == PAGEFLAG_RW);

            const int mmap_flags = VKI_MAP_PRIVATE | VKI_MAP_FIXED | VKI_MAP_ANONYMOUS;
            const int prot = flags_to_mmap_protection(flags);
            SysRes sr = VG_(am_do_mmap_NO_NOTIFY)(base, VKI_PAGE_SIZE, prot, mmap_flags, -1, 0);

            tl_assert(!sr_isError(sr));
            tl_assert(sr._val == base);

            VG_(am_notify_client_mmap)(base, VKI_PAGE_SIZE, prot, mmap_flags, -1, 0);
        }
        if ((flags & PAGEFLAG_RW) == PAGEFLAG_RW)
        {
            for (j = VKI_PAGE_SIZE * i; j < VKI_PAGE_SIZE * (i + 1); j++)
            {
                if (va->vabits[j] & MEM_READ_MASK)
                {
                    dst[j] = src[j];
                }
            }
        }
    }
}
static void memimage_restore_symmap(MemoryImage* memimage)
{
    VG_(OSetGen_Destroy)(current_memspace->symmap);
    current_memspace->symmap = VG_(OSetGen_Create)(offsetof(SymConMapEnt, base), NULL, VG_(malloc), "se.symmap", VG_(free));
    for (Word i = 0; i < memimage->sym_entries_count; i++)
    {
        VG_(OSetGen_Insert)(current_memspace->symmap, &memimage->sym_entries[i]);
    }
}
static void memimage_restore(MemoryImage* memimage)
{
    memimage_restore_symmap(memimage);

    OSet* auxmap = current_memspace->auxmap;
    UWord i = 0;
    AuxMapEnt* elem;

    VG_(OSetGen_ResetIter)(auxmap);
    while(i < memimage->pages_count &&
          (elem = VG_(OSetGen_Next(auxmap))))
    {
        Page* page = memimage->pages[i];
        if (LIKELY(page->base == elem->base))
        {
            if (elem->page != page)
            {
                memimage_restore_page_content(page, elem->page);
                page->ref_count++;
                page_dispose(elem->page);
                elem->page = page;
            }
            i++;
        }
        else
        {
            tl_assert(page->base > elem->base);
            page_dispose(elem->page);
            elem->page = page_new_empty(elem->base);
        }
    }
    tl_assert(i == memimage->pages_count);
    while ((elem = VG_(OSetGen_Next(auxmap))))
    {
        page_dispose(elem->page);
        elem->page = page_new_empty(elem->base);
    }

    VG_(deleteXA)(current_memspace->allocation_blocks);
    current_memspace->allocation_blocks = VG_(cloneXA)("se.allocations",
                                                       memimage->allocation_blocks);

    // reset cache
    current_memspace->page_cache_size = 0;
}

State* state_save_current(void)
{
    State* state = VG_(malloc)("se.state", sizeof(State));
    VG_(memset)(state, 0, sizeof(State));
    state->id = make_new_id();

    // thread state
    ThreadId tid = VG_(get_running_tid());
    tl_assert(tid == 1); // No forking supported yet
    ThreadState* tst = VG_(get_ThreadState)(tid);
    tl_assert(tst);
    tl_assert(tst->status != VgTs_Empty);
    tl_assert(tst->sig_queue == NULL); // TODO: handle non null sig_qeue
    VG_(memcpy)(&state->threadstate, tst, sizeof(ThreadState));

    // memory state
    memimage_save(&state->memimage);

    // register state
    VG_(memcpy)(state->temporaries, temporaries, sizeof(temporaries));
    VG_(memcpy)(state->registerVabits, registerVabits, sizeof(registerVabits));
    VG_(memcpy)(state->registerSbits, registerSbits, sizeof(registerSbits));

    return state;
}
void state_restore(State* state)
{
    // restore thread
    ThreadState* tst = VG_(get_ThreadState)(1);
    tl_assert(tst);
    tl_assert(tst->sig_queue == NULL); // TODO: handle non null sig_qeue

    Int lwpid = tst->os_state.lwpid;
    VG_(memcpy)(tst, &state->threadstate, sizeof(ThreadState));
    tst->os_state.lwpid = lwpid;

    // restore memory state
    memimage_restore(&state->memimage);

    // restore register state
    VG_(memcpy)(temporaries, state->temporaries, sizeof(temporaries));
    VG_(memcpy)(registerVabits, state->registerVabits, sizeof(registerVabits));
    VG_(memcpy)(registerSbits, state->registerSbits, sizeof(registerSbits));
}
