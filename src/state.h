#ifndef VALGRIND_STATE_H
#define VALGRIND_STATE_H

#include "common.h"
#include "memory.h"

#include "../../coregrind/pub_core_aspacemgr.h"
#include "../../coregrind/pub_core_threadstate.h"
#include "pub_tool_oset.h"
#include "expr.h"
#include "symbolic.h"

extern SysRes ML_(am_do_munmap_NO_NOTIFY)(Addr start, SizeT length);

typedef struct {
    Page **pages;
    UWord pages_count;
    XArray *allocation_blocks;
    SymConMapEnt* sym_entries;
    UWord sym_entries_count;
} MemoryImage;

// First two entries has to correspond to VgHashNode
typedef struct {
    struct State *next;
    UWord id;
    MemoryImage memimage;
    ThreadState threadstate;
    RegArea temporaries[TEMPORARIES_COUNT];
    UChar registerVabits[REGISTER_MEMORY_SIZE];
    UChar registerSbits[REGISTER_MEMORY_SIZE];
} State;

State* state_save_current(void);
void state_restore(State *state);

#endif //VALGRIND_STATE_H
