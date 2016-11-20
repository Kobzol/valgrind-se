#include "expr.h"
#include "memory.h"

#define REG_AREA_SIZE 32
#define TMP_AREA_COUNT 4096

// TODO: store this into state
typedef struct
{
    UChar value[REG_AREA_SIZE];
    UChar vabits[REG_AREA_SIZE];
    UChar sbits[REG_AREA_SIZE];
} RegArea;

#define EXPR_MASK ((1 << 13) - 1)
#define IS_BINOP(tag) (tag & (1 << 63))

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
        unsigned int tagOp : 31;
        unsigned char binaryOp : 1;
    };
    HWord value;
} CombinedTag;


static RegArea const_area;
static RegArea temporaries[TMP_AREA_COUNT];

static void expr_reg_load(HWord offset, SizeT size, ExprData* data)
{
    static RegArea area;
    tl_assert(size <= REG_AREA_SIZE);

    VG_(get_shadow_regs_area)(VG_(get_running_tid()), (UChar*) &area, REG_SHADOW, offset, sizeof(RegArea));

    data->value = area.value;
    data->vabits = area.vabits;
    data->sbits = area.sbits;
    data->length = size;
}
static void expr_const_load(HWord constant, SizeT size, ExprData* data)
{
    tl_assert(size <= REG_AREA_SIZE);

    VG_(memcpy)(const_area.value, &constant, size); // copy constant to const area

    data->value = const_area.value;
    data->vabits = const_area.vabits;
    data->sbits = const_area.sbits;
    data->length = size;
}
static void expr_mem_load(Addr a, SizeT size, ExprData* data)
{
    Page* page = page_find(a);
    Addr offset = page_get_offset(a);

    tl_assert(offset + size < PAGE_SIZE);   // assure we don't go over page boundary

    data->value = (UChar*) a;
    data->vabits = page->va->vabits + offset;
    data->sbits = page->va->sbits + offset;
    data->length = size;
}
static void expr_tmp_load(HWord temp, SizeT size, ExprData* data)
{
    tl_assert(size <= REG_AREA_SIZE);
    tl_assert(temp <= TMP_AREA_COUNT);    // assure we have enough temporaries

    data->value = temporaries[temp].value;
    data->vabits = temporaries[temp].vabits;
    data->sbits = temporaries[temp].sbits;
    data->length = size;
}

static void expr_reg_store(HWord offset, SizeT size, ExprData* data)
{
    RegArea area;
    tl_assert(size <= REG_AREA_SIZE);

    VG_(memcpy)(area.value, data->value, size);
    VG_(memcpy)(area.vabits, data->vabits, size);
    VG_(memcpy)(area.sbits, data->sbits, size);
    VG_(set_shadow_regs_area)(VG_(get_running_tid()), REG_SHADOW, offset, sizeof(RegArea), (UChar*) &area);
}
static void expr_tmp_store(HWord temp, SizeT size, ExprData* data)
{
    tl_assert(size <= REG_AREA_SIZE);
    tl_assert(temp < TMP_AREA_COUNT);

    VG_(memcpy)(temporaries[temp].value, data->value, size);
    VG_(memcpy)(temporaries[temp].vabits, data->vabits, size);
    VG_(memcpy)(temporaries[temp].sbits, data->sbits, size);
}

static void expr_load_binop(IROp binaryOp, ExprData* arg1, ExprData* arg2, ExprData* result)
{
    // TODO
}

void expr_init(void)
{
    tl_assert(sizeof(RegArea) == REG_AREA_SIZE * 3);

    for (int i = 0; i < REG_AREA_SIZE; i++)
    {
        const_area.value[i] = 0;
        const_area.vabits[i] = MEM_UNDEFINED;
        const_area.sbits[i] = SYM_CONCRETE;
    }
}

void expr_load(HWord exprType, HWord identifier, HWord identifier2, HWord size, ExprData* data)
{
    if (IS_BINOP(exprType))
    {
        CombinedTag tag;
        tag.value = exprType;
        CombinedSize combinedSize;
        combinedSize.value = size;

        ExprData data1, data2;
        expr_load(tag.tag1, identifier, identifier2, combinedSize.size[0], &data1);
        expr_load(tag.tag2, identifier2, identifier, combinedSize.size[1], &data2);

        expr_load_binop((IROp) tag.tagOp, &data1, &data2, data);
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
    tag.tagOp = operation;
    tag.binaryOp = True;

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