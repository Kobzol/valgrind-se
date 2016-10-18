//
// Created by kobzol on 9/23/16.
//

#ifndef VALGRIND_COMMON_H
#define VALGRIND_COMMON_H

#include "pub_tool_basics.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_xarray.h"
#include "pub_tool_oset.h"
#include "pub_tool_vki.h"
#include "pub_tool_machine.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcassert.h"
#include "../../coregrind/pub_core_clientstate.h"
#include "../../coregrind/pub_core_mallocfree.h"

#include "../../VEX/pub/libvex_ir.h"

#define PRINT(...) { VG_(printf)(__VA_ARGS__); }

#define INLINE    inline __attribute__((always_inline))
#define NOINLINE __attribute__ ((noinline))

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#endif //VALGRIND_COMMON_H
