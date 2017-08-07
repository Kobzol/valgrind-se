#include "instrument.h"
#include "expr.h"
#include "memory.h"

#include "../../VEX/pub/libvex.h"
#include "../../VEX/pub/libvex_ir.h"
#include "net.h"


static Bool LOG_INSTRUMENTATION = False;

static NOINLINE void report_error_write(HWord identifier, HWord size)
{
    PRINT(LOG_ERROR, "Invalid write at %lx of %ld bytes\n", identifier, size);
}
static NOINLINE void report_error_read(HWord exprType, HWord identifier, HWord size)
{
    switch (exprType)
    {
        case Iex_Load:
            PRINT(LOG_ERROR, "Invalid memory read at %lx of %ld bytes\n", identifier, size);
            break;
        case Iex_RdTmp:
            PRINT(LOG_ERROR, "Invalid tmp read from tmp[%lu] of %ld bytes\n", identifier, size);
            break;
        case Iex_Get:
            PRINT(LOG_ERROR, "Invalid reg read from reg[%lu] of %ld bytes\n", identifier, size);
            break;
        default:
            tl_assert(False);
    }
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
    /// no need to check for validity of loaded data
    /// const is valid by default, get is checked in handle_put and rdTmp in handle_wrTmp

    if (LOG_INSTRUMENTATION)
    {
        PRINT(LOG_DEBUG, "Store, type %lu, addr %lu, source1 %lu, source2 %lu, size %lu\n", exprType, addr, source, source2, size);
    }

    Page *page = page_find_or_null(addr);
    if (UNLIKELY(page == NULL))
    {
        report_error_write(addr, size);
        tl_assert(0); // no return here
    }

    char buffer[128];
    VG_(sprintf)(buffer, "%s 0 %lu", MSG_STORE, size);

    NetMessage msg = net_msg(conn, buffer);
    tl_assert(msg.ok);

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

    // move to next page
    if (UNLIKELY(sz != size))
    {
        handle_store(exprType, page_get_start(addr) + PAGE_SIZE, source, source2, size - sz);
    }
}
static VG_REGPARM(3) void handle_wrtmp(HWord exprType, HWord temp, HWord source, HWord source2, HWord size)
{
    if (LOG_INSTRUMENTATION)
    {
        PRINT(LOG_DEBUG, "WrTmp, type %lu, temp %lu, source1 %lu, source2 %lu, size %lu\n", exprType, temp, source, source2, size);
    }

    ExprData data;
    expr_load(exprType, source, source2, size, &data);

    size = expr_get_size(exprType, size, 1);

    if (data.length == 0)
    {
        report_error_read(exprType, source, size);
        tl_assert(False);
    }

    UChar* va = data.vabits;

    if (UNLIKELY(!(*va & MEM_READ_MASK)))
    {
        if (UNLIKELY(*va == MEM_NOACCESS))
        {
            report_error_read(exprType, source, size);
            tl_assert(False);
        }

        // Zero undefined memory before reading
        if (exprType == Iex_Load)
        {
            SizeT i;
            Page* page = page_prepare_for_write_va(page_find(source));
            Addr offset = page_get_offset(source);
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

    expr_store(Ist_WrTmp, temp, size, &data);
}
static VG_REGPARM(3) void handle_put(HWord exprType, HWord destinationOffset, HWord source, HWord source2, HWord size)
{
    if (LOG_INSTRUMENTATION)
    {
        PRINT(LOG_DEBUG, "Put, type %lu, offset %lu, source1 %lu, source2 %lu, size %lu\n", exprType, destinationOffset, source, source2, size);
    }

    ExprData data;
    expr_load(exprType, source, source2, size, &data);
    expr_store(Ist_Put, destinationOffset, size, &data);
}
static VG_REGPARM(3) void handle_dirty(HWord exprType, HWord destinationOffset, HWord source, HWord source2, HWord size)
{
    if (LOG_INSTRUMENTATION)
    {
        PRINT(LOG_DEBUG, "Dirty, type %lu, target tmp %lu, size %lu\n", exprType, destinationOffset, size);
    }

    ExprData data;
    UChar sbits[REGISTER_SIZE];
    UChar vabits[REGISTER_SIZE];

    for (Int i = 0; i < REGISTER_SIZE; i++)
    {
        sbits[i] = SYM_CONCRETE;
        vabits[i] = MEM_DEFINED;
    }

    data.length = size;
    data.sbits = sbits;
    data.vabits = vabits;
    data.remaining = 0;

    expr_store(Ist_WrTmp, destinationOffset, size, &data);
}

// return tmp number from Iex_RdTmp, constant from Iex_Const, offset from Get or load address from Load
static IRExpr* get_expr_identifier(IRExpr* data)
{
    HWord value = 0;

    switch (data->tag)
    {
        case Iex_RdTmp:
            value = data->Iex.RdTmp.tmp;
            break;
        case Iex_Const:
            value = data->Iex.Const.con->Ico.U64;    // TODO
            break;
        case Iex_Get:
            value = (HWord) data->Iex.Get.offset;
            break;
        case Iex_Load:
            return data->Iex.Load.addr;
        case Iex_ITE:
            return data->Iex.ITE.iftrue; // TODO
        default:
            break;
    }

    return mkIRExpr_HWord(value);
}

static void trace_put(IRSB* sb_out, IRTypeEnv* tyenv, IRStmt* st)
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
}
static void trace_store(IRSB* sb_out, IRTypeEnv* tyenv, IRStmt* st)
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
}
static void trace_dirty(IRSB* sb_out, IRTypeEnv* tyenv, IRStmt* st)
{
    // check mFx

    if (st->Ist.Dirty.details->tmp != -1)
    {
        event_add(sb_out,
                  "handle_dirty",
                  st->tag,
                  mkIRExpr_HWord(st->Ist.Dirty.details->tmp),
                  mkIRExpr_HWord(0),
                  mkIRExpr_HWord(0),
                  (HWord) REGISTER_SIZE,
                  handle_dirty);
    }
}
static void trace_wrtmp(IRSB* sb_out, IRTypeEnv* tyenv, IRStmt* st)
{
    IRExpr* data = st->Ist.WrTmp.data;
    IRType type = typeOfIRExpr(tyenv, data);
    tl_assert(type != Ity_INVALID);

    IRTemp destination = st->Ist.WrTmp.tmp;
    HWord size = type == Ity_I1 ? 1 : (HWord) sizeofIRType(type);
    HWord tag = data->tag;
    IRExpr* source1 = get_expr_identifier(data);
    IRExpr* source2 = mkIRExpr_HWord(0);

    if (tag == Iex_Unop)
    {
        tag = data->Iex.Unop.arg->tag;

        source1 = get_expr_identifier(data->Iex.Unop.arg);
        source2 = mkIRExpr_HWord(data->Iex.Unop.op);

        type = typeOfIRExpr(tyenv, data->Iex.Unop.arg);
        size = type == Ity_I1 ? 1 : (HWord) sizeofIRType(type);
    }
    else if (tag == Iex_ITE)    // TODO
    {
        tag = data->Iex.ITE.iftrue->tag;
        source1 = get_expr_identifier(data->Iex.ITE.iftrue);
        type = typeOfIRExpr(tyenv, data->Iex.ITE.iftrue);
        size = type == Ity_I1 ? 1 : (HWord) sizeofIRType(type);
    }
    else if (tag == Iex_Binop)
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

    //ppIRSB(sb_in);

    sb_out = deepCopyIRSBExceptStmts(sb_in);

    i = 0;
    while (i < sb_in->stmts_used && sb_in->stmts[i]->tag != Ist_IMark)
    {
        addStmtToIRSB(sb_out, sb_in->stmts[i]);
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
                HWord tag = st->Ist.WrTmp.data->tag;

                if (tag == Iex_Get ||
                    tag == Iex_RdTmp ||
                    tag == Iex_Unop ||
                    tag == Iex_Binop ||
                    tag == Iex_Load ||
                    tag == Iex_Const ||
                    tag == Iex_ITE)
                {
                    trace_wrtmp(sb_out, tyenv, st);
                }

                addStmtToIRSB(sb_out, st);
                break;
            }
            case Ist_Put:
            {
                trace_put(sb_out, tyenv, st);
                addStmtToIRSB(sb_out, st);
                break;
            }
            case Ist_Store:
            {
                trace_store(sb_out, tyenv, st);
                addStmtToIRSB(sb_out, st);
                break;
            }
            case Ist_Dirty:
            {
                trace_dirty(sb_out, tyenv, st);
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