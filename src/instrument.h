#ifndef VALGRIND_INSTRUMENT_H
#define VALGRIND_INSTRUMENT_H

#include "common.h"

IRSB* se_instrument(VgCallbackClosure* closure,
                    IRSB* sb_in,
                    const VexGuestLayout* layout,
                    const VexGuestExtents* vge,
                    const VexArchInfo* archinfo_host,
                    IRType gWordTy,
                    IRType hWordTy);

#endif //VALGRIND_INSTRUMENT_H
