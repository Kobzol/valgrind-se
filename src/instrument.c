#include "instrument.h"

static NOINLINE void report_error_write(Addr addr, SizeT size)
{
    PRINT(LOG_ERROR, "Invalid write at %lx of %d bytes\n", addr, size);
}
static NOINLINE void report_error_read(Addr addr, SizeT size)
{
    PRINT(LOG_ERROR, "Invalid read at %lx of %d bytes\n", addr, size);
}

static void event_addr_int(IRSB* sb,
                           const char* name,
                           IRExpr* addr, Int i,
                           void (*fn)(Addr, HWord))
{
    IRExpr** args = mkIRExprVec_2(addr, mkIRExpr_HWord(i));
    IRDirty* di = unsafeIRDirty_0_N(2, name, VG_(fnptr_to_fnentry)(fn), args);
    addStmtToIRSB(sb, IRStmt_Dirty(di));
}

static VG_REGPARM(2) void handle_write(Addr addr, SizeT size)
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
        handle_write(page_get_start(addr) + PAGE_SIZE, size - sz);
    }
}
static VG_REGPARM(2) void handle_read(Addr addr, SizeT size)
{
    Page *page = page_find(addr);
    if (UNLIKELY(page == NULL))
    {
        report_error_read(addr, size);
        tl_assert(0); // no return here
    }
    Addr offset = addr - page->base;

    if (UNLIKELY(!(page->va->vabits[offset] & MEM_READ_MASK)))
    {
        if (UNLIKELY(page->va->vabits[offset] == MEM_NOACCESS))
        {
            report_error_read(addr, size);
            tl_assert(0); // no return here
        }

        SizeT i; // Zero undefined memory before reading
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
                IRTemp tmp = st->Ist.WrTmp.tmp;
                IRExpr* data = st->Ist.WrTmp.data;
                switch (data->tag)
                {
                    case Iex_Load:
                        event_addr_int(sb_out,
                                       "handle_read",
                                       data->Iex.Load.addr,
                                       sizeofIRType(data->Iex.Load.ty),
                                       handle_read);
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
                event_addr_int(sb_out, "handle_write", st->Ist.Store.addr, sizeofIRType(type), handle_write);
                addStmtToIRSB(sb_out, st);
                break;
            }
        /*case Ist_Store: {
            IRExpr* data = st->Ist.Store.data;
            IRType  type = typeOfIRExpr(tyenv, data);
            tl_assert(type != Ity_INVALID);
            event_write(sb_out, st->Ist.Store.addr, data, sizeofIRType(type));
            addStmtToIRSB(sb_out, st);
            break;
        }
        /*case Ist_Put: {
            Int offset = st->Ist.Put.offset;
            IRExpr* data = st->Ist.Put.data;
            switch (data->tag)
            {
                case Iex_RdTmp:
                {
                    event_offset(sb_out, offset, data->Iex.RdTmp.tmp);
                    break;
                }
                case Iex_Const:
                {
                    //event_offset(sb_out, offset, data->Iex.Const.con->Ico.U32);
                    break;
                }
            }
            addStmtToIRSB(sb_out, st);
            break;
        }*/

        default:
            addStmtToIRSB(sb_out, st);
            break;
        }
    }

    return sb_out;
}