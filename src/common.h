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
#include "pub_tool_libcfile.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_threadstate.h"
#include "../../coregrind/pub_core_clientstate.h"
#include "../../coregrind/pub_core_libcfile.h"
#include "../../coregrind/pub_core_mallocfree.h"

#include "../../VEX/pub/libvex_ir.h"

#define PRINT(level, ...) { if (verbosity_level <= (level)) { VG_(printf)(__VA_ARGS__); } }
#define INLINE    inline __attribute__((always_inline))

#define WAIT() { char buf[16]; VG_(read)(0, buf, 1);  }
#define NOINLINE __attribute__ ((noinline))

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define LOG_DEBUG 0
#define LOG_INFO 1
#define LOG_WARNING 2
#define LOG_ERROR 3


extern int verbosity_level;

#endif //VALGRIND_COMMON_H
