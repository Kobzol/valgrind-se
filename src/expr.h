#ifndef VALGRIND_EXPR_H
#define VALGRIND_EXPR_H

#include "common.h"

#define REGISTER_SIZE 256
#define TEMPORARIES_COUNT 4096
#define REGISTER_MEMORY_SIZE (REGISTER_SIZE * 128)

// TODO: store this into state
typedef struct
{ ;
    UChar vabits[REGISTER_SIZE];
    UChar sbits[REGISTER_SIZE];
} RegArea;

typedef union
{
    unsigned int size[2];
    HWord value;
} CombinedSize;

typedef union
{
    struct
    {
        unsigned int tag1 : 16;
        unsigned int tag2 : 16;
        unsigned int opType : 31;
        unsigned char isBinaryOpTag : 1;
    };
    HWord value;
} CombinedTag;

typedef struct
{
    UChar* vabits;
    UChar* sbits;
    SizeT length;
    SizeT remaining;
} ExprData;

extern RegArea temporaries[TEMPORARIES_COUNT];
extern UChar registerVabits[REGISTER_MEMORY_SIZE];
extern UChar registerSbits[REGISTER_MEMORY_SIZE];


void expr_init(void);

/// Get -> identifier = register offset
/// Load -> identifier = load address
/// Const -> identifier = constant
/// RdTmp -> identifier = temp number
void expr_load(HWord exprType, HWord identifier, HWord identifier2, HWord size, ExprData* data);

SizeT expr_get_size(HWord exprType, SizeT size, int index);

/// Put -> identifier = register offset
/// Store -> identifier = address
/// WrTmp -> identifier = temp number
void expr_store(HWord exprType, HWord destination, HWord size, ExprData* data);

HWord expr_pack_binop_tag(IROp operation, Int tag1, Int tag2);
HWord expr_pack_binop_size(Int size1, Int size2);

#endif //VALGRIND_EXPR_H
