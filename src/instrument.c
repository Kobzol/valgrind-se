#include "instrument.h"

static NOINLINE void report_error_write(Addr addr, SizeT size)
{
    PRINT(LOG_ERROR, "Invalid write at %lx of %ld bytes\n", addr, size);
}
static NOINLINE void report_error_read(Addr addr, SizeT size)
{
    PRINT(LOG_ERROR, "Invalid read at %lx of %ld bytes\n", addr, size);
}

static void event_add(IRSB* sb,
                      const char* name,
                      Int exprType,
                      IRExpr* addr,
                      Int i1,
                      Int i2,
                      void (*fn)(Int, Addr, SizeT, HWord))
{
    IRExpr** args = mkIRExprVec_4(mkIRExpr_HWord(exprType), addr, mkIRExpr_HWord(i1), mkIRExpr_HWord(i2));
    IRDirty* di = unsafeIRDirty_0_N(3, name, VG_(fnptr_to_fnentry)(fn), args);
    addStmtToIRSB(sb, IRStmt_Dirty(di));
}

static UChar reg_va_buffer[512];
static UChar* load_expr_get(HWord offset, SizeT size)
{
    VG_(get_shadow_regs_area)(VG_(get_running_tid()), reg_va_buffer, REG_SHADOW, offset, size);
    return reg_va_buffer;
}
static UChar* load_expr_gen(Int exprType, Addr a, SizeT size, HWord i1, Int* loadedSize)
{
    *loadedSize = size;

    if (exprType == Iex_Get)
    {
        return load_expr_get(i1, size);
    }
    else if (exprType == Iex_Const)
    {
        return &(uniform_va[MEM_DEFINED]->vabits[0]);
    }
    else if (exprType == Iex_RdTmp)
    {
        // TODO
        PRINT(LOG_DEBUG, "Iex_RdTmp\n");
    }
    else if (exprType == Iex_Load)
    {
        return page_get_va(a, size, loadedSize);
    }

    return NULL;
}

static VG_REGPARM(3) void handle_store(Int exprType, Addr addr, SizeT size, HWord i1)
{
    Page *page = page_find_or_null(addr);
    if (UNLIKELY(page == NULL))
    {
        report_error_write(addr, size);
        tl_assert(0); // no return here
    }

    page = page_prepare_for_write_data(page);
    VA *va = page->va;
    Addr offset = addr - page->base;
    SizeT i;

    UChar flag = va->vabits[offset];
    SizeT sz = size;

    if (UNLIKELY(offset + sz > PAGE_SIZE))
    {
        sz = PAGE_SIZE - offset;
    }

    for (i = 1; i < sz; i++)
    {
        flag &= va->vabits[offset + i];
    }

    if (UNLIKELY(!(flag & MEM_WRITE_MASK)))
    {
        report_error_write(addr, size);
        tl_assert(0); // no return here
    }

    if (flag == MEM_UNDEFINED)
    {
        if (UNLIKELY(va->ref_count >= 2))
        {
            va->ref_count--;
            va = va_clone(va);
            page->va = va;
        }
        for (i = 0; i < sz; i++)
        {
            va->vabits[offset + i] = MEM_DEFINED;
        }
    }

    if (UNLIKELY(sz != size))
    {
        handle_store(exprType, page_get_start(addr) + PAGE_SIZE, size - sz, i1);
    }
}
static VG_REGPARM(3) void handle_wrtmp(Int exprType, Addr addr, SizeT size, HWord i1)
{
    Page *page = page_find(addr);
    if (UNLIKELY(page == NULL))
    {
        report_error_read(addr, size);
        tl_assert(0); // no return here
    }
    Addr offset = addr - page->base;

    tl_assert(offset + size <= PAGE_SIZE);

    Int loadedSize;
    UChar* va = load_expr_gen(exprType, addr, size, i1, &loadedSize);

    if (UNLIKELY(!(*va & MEM_READ_MASK)))
    {
        if (UNLIKELY(*va == MEM_NOACCESS))
        {
            report_error_read(addr, size);
            tl_assert(0); // no return here
        }

        if (exprType == Iex_Load)
        {
            SizeT i; // Zero undefined memory before reading
            page = page_prepare_for_write_va(page);
            for (i = 0; i < size; i++)
            {
                if (page->va->vabits[offset + i] == MEM_UNDEFINED)
                {
                    HChar *d = (HChar*) (offset + i + page->base);
                    *d = 0;
                }
            }
        }
    }
}
/// i1 - reg offset
static VG_REGPARM(3) void handle_put(Int exprType, Addr addr, SizeT size, HWord i1)
{
    ThreadId tid = VG_(get_running_tid());
    Int loadedSize;
    UChar* va = load_expr_gen(exprType, addr, size, i1, &loadedSize);
    VG_(set_shadow_regs_area)(tid, REG_SHADOW, i1, loadedSize, va);
}

IRSB* se_instrument(VgCallbackClosure* closure,
                    IRSB* sb_in,
                    const VexGuestLayout* layout,
                    const VexGuestExtents* vge,
                    const VexArchInfo* archinfo_host,
                    IRType gWordTy,
                    IRType hWordTy)
{
    Int i;
    IRSB *sb_out;
    IRTypeEnv* tyenv = sb_in->tyenv;

    sb_out = deepCopyIRSBExceptStmts(sb_in);

    i = 0;
    while (i < sb_in->stmts_used && sb_in->stmts[i]->tag != Ist_IMark)
    {
        addStmtToIRSB( sb_out, sb_in->stmts[i] );
        i++;
    }

    for (; i < sb_in->stmts_used; i++)
    {
        IRStmt* st = sb_in->stmts[i];
        switch (st->tag)
        {
            case Ist_WrTmp:
            {
                IRExpr* data = st->Ist.WrTmp.data;
                IRType type = typeOfIRExpr(tyenv, data);
                switch (data->tag)
                {
                    case Iex_Load:
                        event_add(sb_out,
                                  "handle_wrtmp",
                                  Iex_Load,
                                  data->Iex.Load.addr,
                                  sizeofIRType(type),
                                  0, // unused
                                  handle_wrtmp);
                        break;
                    default:
                        break;
                }
                addStmtToIRSB(sb_out, st);
                break;
            }
            case Ist_Put:
            {
                IRExpr* data = st->Ist.Put.data;
                IRType type = typeOfIRExpr(tyenv, data);
                Int offset = st->Ist.Put.offset;
                switch (data->tag)
                {
                    case Iex_Load:
                    {
                        event_add(sb_out,
                                           "handle_put",
                                           Iex_Load,
                                           data->Iex.Load.addr,
                                           sizeofIRType(type),
                                           offset,
                                           handle_put);
                        break;
                    }
                    default:
                        break;
                }

                addStmtToIRSB(sb_out, st);
                break;
            }
            case Ist_Store:
            {
                IRExpr* data = st->Ist.Store.data;
                IRType  type = typeOfIRExpr(tyenv, data);
                tl_assert(type != Ity_INVALID);
                event_add(sb_out,
                               "handle_store",
                               Iex_Load,
                               st->Ist.Store.addr,
                               sizeofIRType(type),
                               0, // unused
                               handle_store);
                addStmtToIRSB(sb_out, st);
                break;
            }

        default:
            addStmtToIRSB(sb_out, st);
            break;
        }
    }

    return sb_out;
}