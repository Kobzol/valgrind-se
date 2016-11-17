#ifndef VALGRIND_STATE_H
#define VALGRIND_STATE_H

#include "common.h"
#include "memory.h"

#include "../../coregrind/pub_core_aspacemgr.h"
#include "../../coregrind/pub_core_threadstate.h"

extern SysRes ML_(am_do_munmap_NO_NOTIFY)(Addr start, SizeT length);

typedef struct {
    Page **pages;
    UWord pages_count;
    XArray *allocation_blocks;
} MemoryImage;

// First two entries has to correspond to VgHashNode
typedef struct {
    struct State *next;
    UWord id;
    MemoryImage memimage;
    ThreadState threadstate;
} State;

State* state_save_current(void);
void state_restore(State *state);

#endif //VALGRIND_STATE_H