#include "se_client.h"

#include "stdio.h"
#include "string.h"


static void se_call(Vg_SERequestType type, SEArgType *args, SEArgType count)
{
    if (VALGRIND_DO_CLIENT_REQUEST_EXPR(1, VG_USERREQ__SE_CALL, type, args, count, NULL, NULL))
    {
        fprintf(stderr, "This application is not supported (SE).");
    }
}

void se_save_state(void)
{
    se_call(VG_USERREQ__SE_SAVE_STATE, NULL, 0);
}
void se_restore_state(void)
{
    se_call(VG_USERREQ__SE_RESTORE_STATE, NULL, 0);
}
void se_make_symbolic(void* mem, int size)
{
    SEArgType args[2] = { (SEArgType) mem, (SEArgType) size };
    se_call(VG_USERREQ__SE_MAKE_SYMBOLIC, args, 2);
}
