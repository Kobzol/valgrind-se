#pragma once

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include "../../include/valgrind.h"

typedef unsigned long SEArgType; // Has to be the same type as UWord

typedef enum {
    VG_USERREQ__SE_CALL = VG_USERREQ_TOOL_BASE('S','E')
} Vg_SEClientRequest;

typedef enum {
    VG_USERREQ__SE_SAVE_STATE = 1,
    VG_USERREQ__SE_RESTORE_STATE = 2
} Vg_SERequestType;

void se_save_state(void);
void se_restore_state(void);

#ifdef __cplusplus
}
#endif // __cplusplus
