#include "instrument.h"

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
            case Ist_Store:
            {
                IRExpr* data = st->Ist.Store.data;
                IRType  type = typeOfIRExpr(tyenv, data);
                tl_assert(type != Ity_INVALID);
                event_addr_int(sb_out, "handle_write", st->Ist.Store.addr, sizeofIRType(type), handle_write);
                addStmtToIRSB(sb_out, st);
                break;
            }
        /*case Ist_WrTmp:
        {
            IRTemp tmp = st->Ist.WrTmp.tmp;
            IRExpr* data = st->Ist.WrTmp.data;
            switch (data->tag)
            {
                case Iex_Load:
                    event_addr_int_int(sb_out,
                                  "trace_read",
                                  data->Iex.Load.addr,
                                  sizeofIRType(data->Iex.Load.ty),
                                  tmp, trace_tmp);
                    break;
                default:
                {
                    PRINT("tmp and not load\n");
                    break;
                }
            }
            addStmtToIRSB(sb_out, st);
            break;
        }
        case Ist_Store: {
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