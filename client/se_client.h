#pragma once

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include "../../include/valgrind.h"

typedef unsigned long SEArgType; // Has to be the same type as UWord

typedef
enum {
    VG_USERREQ__SE_CALL = VG_USERREQ_TOOL_BASE('S','E')
} Vg_SEClientRequest;

typedef
enum {
    VG_USERREQ__SE_MAKE_SYMBOLIC = 1
} Vg_SERequestType;

void se_make_symbolic(void* mem, int size);

#ifdef __cplusplus
}
#endif // __cplusplus
