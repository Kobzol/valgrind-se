#include "instrument.h"
#include "expr.h"

#include "../../VEX/pub/libvex_ir.h"
#include "../../VEX/pub/libvex.h"

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
                      HWord exprType,
                      IRExpr* destination,
                      IRExpr* source,
                      IRExpr* source2,
                      HWord size,
                      void (*fn)(HWord, HWord, HWord, HWord, HWord))
{
    IRExpr** args = mkIRExprVec_5(mkIRExpr_HWord(exprType), destination, source, source2, mkIRExpr_HWord(size));
    IRDirty* di = unsafeIRDirty_0_N(3, name, VG_(fnptr_to_fnentry)(fn), args);
    addStmtToIRSB(sb, IRStmt_Dirty(di));
}

static VG_REGPARM(3) void handle_store(HWord exprType, HWord addr, HWord source, HWord source2, HWord size)
{
    /*Page *page = page_find_or_null(addr);
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
    }*/
}
static VG_REGPARM(3) void handle_wrtmp(HWord exprType, HWord temp, HWord source, HWord source2, HWord size)
{
    /*ExprData data;
    expr_load(exprType, source, size, &data);
    expr_store(Ist_WrTmp, temp, size, &data);

    Page *page = page_find(addr);
    if (UNLIKELY(page == NULL))
    {
        report_error_read(addr, size);
        tl_assert(0); // no return here
    }
    Addr offset = addr - page->base;

    tl_assert(offset + size <= PAGE_SIZE);

    Int loadedSize; TODO
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
    }*/
}
static VG_REGPARM(3) void handle_put(HWord exprType, HWord destinationOffset, HWord source, HWord source2, HWord size)
{
    ExprData data;
    expr_load(exprType, source, source2, size, &data);
    expr_store(Ist_Put, destinationOffset, size, &data);
}

// return tmp number from Iex_RdTmp, constant from Iex_Const, offset from Get or load address from Load
static IRExpr* get_expr_identifier(IRExpr* data)
{
    HWord value = 0;

    if (data->tag == Iex_RdTmp)
    {
        value = data->Iex.RdTmp.tmp;
    }
    else if (data->tag == Iex_Const)
    {
        value = data->Iex.Const.con->Ico.U64;    // TODO
    }
    else if (data->tag == Iex_Get)
    {
        value = (HWord) data->Iex.Get.offset;
    }
    else if (data->tag == Iex_Load)
    {
        return data->Iex.Load.addr;
    }

    return mkIRExpr_HWord(value);
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
            /*case Ist_Exit:
            {
                if (st->Ist.Exit.jk == Ijk_Boring)
                {
                    SizeT target = util_get_const(st->Ist.Exit.dst);
                    IRExpr* guard = st->Ist.Exit.guard;
                    IRType type = typeOfIRExpr(tyenv, guard);

                    event_add(sb_out,
                        "handle_jump",
                    );
                }

                addStmtToIRSB(sb_out, st);
                break;
            }*/
            case Ist_WrTmp:
            {
                IRExpr* data = st->Ist.WrTmp.data;
                IRType type = typeOfIRExpr(tyenv, data);
                tl_assert(type != Ity_INVALID);

                IRTemp destination = st->Ist.WrTmp.tmp;
                HWord size = type == Ity_I1 ? 1 : (HWord) sizeofIRType(type);
                HWord tag = data->tag;
                IRExpr* source1 = get_expr_identifier(data);
                IRExpr* source2 = mkIRExpr_HWord(0);

                if (data->tag == Iex_Binop)
                {
                    Int tag1 = data->Iex.Binop.arg1->tag;
                    Int tag2 = data->Iex.Binop.arg2->tag;

                    tl_assert(tag1 == Iex_Const || tag1 == Iex_RdTmp);
                    tl_assert(tag2 == Iex_Const || tag2 == Iex_RdTmp);

                    Int size1 = sizeofIRType(typeOfIRExpr(tyenv, data->Iex.Binop.arg1));
                    Int size2 = sizeofIRType(typeOfIRExpr(tyenv, data->Iex.Binop.arg2));

                    size = expr_pack_binop_size(size1, size2);
                    tag = expr_pack_binop_tag(data->Iex.Binop.op, tag1, tag2);
                    source1 = get_expr_identifier(data->Iex.Binop.arg1);
                    source2 = get_expr_identifier(data->Iex.Binop.arg2);
                }

                event_add(sb_out,
                          "handle_wrtmp",
                          tag,
                          mkIRExpr_HWord(destination),
                          source1,
                          source2,
                          size,
                          handle_wrtmp);

                addStmtToIRSB(sb_out, st);
                break;
            }
            case Ist_Put:
            {
                IRExpr* data = st->Ist.Put.data;
                IRType type = typeOfIRExpr(tyenv, data);
                Int offset = st->Ist.Put.offset;
                HWord size = type == Ity_I1 ? 1 : (HWord) sizeofIRType(type);
                tl_assert(data->tag == Iex_Const || data->tag == Iex_RdTmp);

                event_add(sb_out,
                          "handle_put",
                          data->tag,
                          mkIRExpr_HWord((HWord) offset),
                          get_expr_identifier(data),
                          mkIRExpr_HWord(0),
                          size,
                          handle_put);

                addStmtToIRSB(sb_out, st);
                break;
            }
            case Ist_Store:
            {
                IRExpr* data = st->Ist.Store.data;
                IRType  type = typeOfIRExpr(tyenv, data);
                HWord size = type == Ity_I1 ? 1 : (HWord) sizeofIRType(type);

                tl_assert(type != Ity_INVALID);
                tl_assert(data->tag == Iex_Const || data->tag == Iex_RdTmp || data->tag == Iex_Get);

                event_add(sb_out,
                          "handle_store",
                          data->tag,
                          st->Ist.Store.addr,
                          get_expr_identifier(data),
                          mkIRExpr_HWord(0),
                          size,
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