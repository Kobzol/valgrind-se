#include "symbolic.h"

SymConMapEnt* sym_find(Addr base)
{
    SymConMapEnt key;
    key.base = base;
    return VG_(OSetGen_Lookup)(current_memspace->symmap, &key);
}
void sym_insert(Addr base, SizeT constraintId)
{
    SymConMapEnt* node = (SymConMapEnt*) VG_(OSetGen_AllocNode)(current_memspace->symmap, sizeof(SymConMapEnt));
    node->base = base;
    node->constraintId = constraintId;
    VG_(OSetGen_Insert)(current_memspace->symmap, node);
}