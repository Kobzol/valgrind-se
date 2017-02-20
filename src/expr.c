#include "expr.h"
#include "memory.h"
#include "../../VEX/pub/libvex_guest_amd64.h"


static RegArea const_area;
RegArea temporaries[TEMPORARIES_COUNT];
UChar registerVabits[REGISTER_MEMORY_SIZE];
UChar registerSbits[REGISTER_MEMORY_SIZE];


static void expr_reg_load(HWord offset, SizeT size, ExprData* data)
{
    tl_assert(size <= REGISTER_SIZE);
    tl_assert(offset < REGISTER_MEMORY_SIZE); // assure we have enough registers

    data->vabits = registerVabits + offset;
    data->sbits = registerSbits + offset;
    data->length = size;
    data->remaining = 0;
}
static void expr_const_load(HWord constant, SizeT size, ExprData* data)
{
    tl_assert(size <= REGISTER_SIZE);

    data->vabits = const_area.vabits;
    data->sbits = const_area.sbits;
    data->length = size;
    data->remaining = 0;
}
static void expr_mem_load(Addr a, SizeT size, ExprData* data)
{
    Page* page = page_find(a);

    if (page == NULL)
    {
        data->length = 0;
        return;
    }

    Addr offset = page_get_offset(a);

    SizeT length = size;
    SizeT remaining = 0;
    /*if (offset + size >= PAGE_SIZE)
    {
        remaining = (offset + size) - PAGE_SIZE;
        length -= remaining;
    }*/
    tl_assert(offset + length <= PAGE_SIZE);   // assure we don't go over page boundary

    data->vabits = page->va->vabits + offset;
    data->sbits = page->va->sbits + offset;
    data->length = length;
    data->remaining = remaining;
}
static void expr_tmp_load(HWord temp, SizeT size, ExprData* data)
{
    tl_assert(size <= REGISTER_SIZE);
    tl_assert(temp < TEMPORARIES_COUNT);    // assure we have enough temporaries

    data->vabits = temporaries[temp].vabits;
    data->sbits = temporaries[temp].sbits;
    data->length = size;
    data->remaining = 0;
}

static void expr_reg_store(HWord offset, SizeT size, ExprData* data)
{
    tl_assert(size <= REGISTER_SIZE);
    tl_assert(offset < REGISTER_MEMORY_SIZE);

    VG_(memmove)(registerVabits + offset, data->vabits, size);
    VG_(memmove)(registerSbits + offset, data->sbits, size);
}
static void expr_tmp_store(HWord temp, SizeT size, ExprData* data)
{
    tl_assert(size <= REGISTER_SIZE);
    tl_assert(temp < TEMPORARIES_COUNT);

    VG_(memmove)(temporaries[temp].vabits, data->vabits, size);
    VG_(memmove)(temporaries[temp].sbits, data->sbits, size);
}

static void expr_load_binop(IROp binaryOp, ExprData* arg1, ExprData* arg2, ExprData* result)
{
    /*if (binaryOp >= Iop_8Uto16 && binaryOp <= Iop_32Sto64)
    {
        // widening cast
    }*/

    // TODO: check both arguments
    result->vabits = arg1->vabits;
    result->sbits = arg1->sbits;
    result->length = arg1->length;
    result->remaining = 0;
}

void expr_init(void)
{
    tl_assert(sizeof(RegArea) == REGISTER_SIZE * 2);

    for (int i = 0; i < REGISTER_SIZE; i++)
    {
        const_area.vabits[i] = MEM_UNDEFINED;
        const_area.sbits[i] = SYM_CONCRETE;
    }

    for (int i = 0; i < sizeof(registerVabits); i++)
    {
        registerVabits[i] = MEM_DEFINED;
        registerSbits[i] = SYM_CONCRETE;
    }
}

void expr_load(HWord exprType, HWord identifier, HWord identifier2, HWord size, ExprData* data)
{
    CombinedTag tag;
    tag.value = exprType;

    if (tag.isBinaryOpTag)
    {
        CombinedSize combinedSize;
        combinedSize.value = size;

        ExprData data1, data2;
        expr_load(tag.tag1, identifier, identifier2, combinedSize.size[0], &data1);
        expr_load(tag.tag2, identifier2, identifier, combinedSize.size[1], &data2);

        expr_load_binop((IROp) tag.opType, &data1, &data2, data);
    }
    else
    {
        switch (exprType)
        {
            case Iex_Get:
                expr_reg_load(identifier, size, data);
                break;
            case Iex_Const:
                expr_const_load(identifier, size, data);
                break;
            case Iex_RdTmp:
                expr_tmp_load(identifier, size, data);
                break;
            case Iex_Load:
                expr_mem_load(identifier, size, data);
                break;
            default:
                PRINT(LOG_DEBUG, "%lu\n", exprType);
                tl_assert(False && "Unrecognized expr type");
        }
    }
}
SizeT expr_get_size(HWord exprType, SizeT size, int index)
{
    CombinedTag tag;
    tag.value = exprType;

    if (tag.isBinaryOpTag)
    {
        CombinedSize combinedSize;
        combinedSize.value = size;
        return combinedSize.size[index];
    }
    else return size;
}
void expr_store(HWord exprType, HWord destination, HWord size, ExprData* data)
{
    switch (exprType)
    {
        case Ist_Put:
            expr_reg_store(destination, size, data);
            break;
        case Ist_WrTmp:
            expr_tmp_store(destination, size, data);
            break;
        default:
            tl_assert(False && "Unrecognized statement type");
    }
}

HWord expr_pack_binop_tag(IROp operation, Int tag1, Int tag2)
{
    CombinedTag tag;
    tag.value = 0;

    tag.tag1 = (unsigned int) tag1;
    tag.tag2 = (unsigned int) tag2;
    tag.opType = (unsigned int) operation;
    tag.isBinaryOpTag = True;

    return tag.value;
}
HWord expr_pack_binop_size(Int size1, Int size2)
{
    CombinedSize size;
    size.value = 0;

    size.size[0] = (unsigned int) size1;
    size.size[1] = (unsigned int) size2;

    return size.value;
}