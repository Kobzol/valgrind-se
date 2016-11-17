#include "common.h"

int verbosity_level = LOG_DEBUG;

SizeT util_get_const(IRConst* constant)
{
    switch (constant->tag)
    {
        case Ico_U1:
            return constant->Ico.U1;
        case Ico_U8:
            return constant->Ico.U8;
        case Ico_U16:
            return constant->Ico.U16;
        case Ico_U32:
            return constant->Ico.U32;
        case Ico_U64:
            return constant->Ico.U64;
        default:
            tl_assert(0);
            return 0;
    }
}
