#ifndef VALGRIND_EXPR_H
#define VALGRIND_EXPR_H

#include "common.h"

typedef struct
{
    UChar* value;
    UChar* vabits;
    UChar* sbits;
    SizeT length;
} ExprData;


void expr_init(void);

/// Get -> identifier = register offset
/// Load -> identifier = load address
/// Const -> identifier = constant
/// RdTmp -> identifier = temp number
void expr_load(HWord exprType, HWord identifier, HWord identifier2, HWord size, ExprData* data);

/// Put -> identifier = register offset
/// Store -> identifier = address
/// WrTmp -> identifier = temp number
void expr_store(HWord exprType, HWord destination, HWord size, ExprData* data);

HWord expr_pack_binop_tag(IROp operation, Int tag1, Int tag2);
HWord expr_pack_binop_size(Int size1, Int size2);

#endif //VALGRIND_EXPR_H
