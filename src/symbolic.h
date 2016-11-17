#ifndef VALGRIND_SYMBOLIC_H
#define VALGRIND_SYMBOLIC_H

#include "common.h"
#include "memory.h"

typedef struct
{
    Addr base;
    SizeT constraintId;
} SymConMapEnt;

SymConMapEnt* sym_find(Addr base);
void sym_insert(Addr base, SizeT constraintId);


#endif //VALGRIND_SYMBOLIC_H
