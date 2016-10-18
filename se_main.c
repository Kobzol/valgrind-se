
/*--------------------------------------------------------------------*/
/*--- Nulgrind: The minimal Valgrind tool.               se_main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Nulgrind, the minimal Valgrind tool,
   which does no instrumentation or analysis.

   Copyright (C) 2002-2015 Nicholas Nethercote
      njn@valgrind.org

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

#include "pub_tool_basics.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_oset.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_debuginfo.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_options.h"
#include "pub_tool_basics.h"
#include "pub_tool_threadstate.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_hashtable.h"
#include "pub_tool_xarray.h"
#include "pub_tool_debuginfo.h"
#include "pub_tool_stacktrace.h"
#include "pub_tool_replacemalloc.h"
#include "pub_tool_transtab.h"
#include "pub_tool_machine.h"     // VG_(fnptr_to_fnentry)
#include "pub_tool_vki.h"
#include "valgrind.h"

#include "../VEX/pub/libvex_ir.h"

#include "client/se_client.h"
#include "src/common.h"
#include "src/syscall.h"
#include "src/memory.h"
#include "src/instrument.h"

char MEMORY[4096] = {0};
int INDEX = 0;

static void se_post_clo_init(void)
{
}

static void se_fini(Int exitcode)
{
}

static void* se_client_malloc(ThreadId tid, SizeT n)
{
    void *mem = (void*)(MEMORY + INDEX);
    INDEX += n;
    return mem;
}

static
void* se_client_memalign(ThreadId tid, SizeT alignB, SizeT n)
{
    SizeT mod = INDEX % alignB;
    if (mod != 0)
    {
        INDEX += alignB - mod;
    }

    return se_client_malloc(tid, n);
}

static
void* se_client_calloc(ThreadId tid, SizeT nmemb, SizeT size1)
{
    return se_client_malloc(tid, nmemb);
}

static
void* se_client_realloc(ThreadId tid, void* p_old, SizeT new_szB)
{
    return p_old;
}

static
SizeT se_client_malloc_usable_size(ThreadId tid, void* p)
{
    return 512;
}


static void se_client_free (ThreadId tid, void *a)
{
    // no-op
}

static
Bool se_handle_client_request (ThreadId tid, UWord* args, UWord* ret)
{
    if (!VG_IS_TOOL_USERREQ('S','E', args[0]))
    {
        return False;
    }

    SEArgType* requestArgs = args[2];
    SEArgType argsSize = args[3];

    switch (args[1])
    {
        case VG_USERREQ__SE_MAKE_SYMBOLIC:
        {
            void *addr = (void *) requestArgs[0];
            SEArgType size = requestArgs[1];

            // TODO: make memory symbolic

            break;
        }
        default:
        {
            tl_assert(False);
            break;
        }
    }

    *ret = 0;
    return True;
}

static void se_pre_clo_init(void)
{
    VG_(details_name)            ("Segrind 3 test");
    VG_(details_version)         (NULL);
    VG_(details_description)     ("the symbolic Valgrind tool");
    VG_(details_copyright_author)(
      "Copyright (C) 2002-2015, and GNU GPL'd, by Nicholas Nethercote.");
    VG_(details_bug_reports_to)  (VG_BUGS_TO);

    VG_(details_avg_translation_sizeB) ( 275 );

    VG_(needs_syscall_wrapper)(syscall_handle_pre, syscall_handle_post);

    VG_(needs_malloc_replacement)  (se_client_malloc,
                                    se_client_malloc, //MC_(__builtin_new),
                                    se_client_malloc, //MC_(__builtin_vec_new),
                                    se_client_memalign, //MC_(memalign),
                                    se_client_calloc, //MC_(calloc),
                                    se_client_free, //MC_(free),
                                    se_client_free, //MC_(__builtin_delete),
                                    se_client_free, //MC_(__builtin_vec_delete),
                                    se_client_realloc, //MC_(realloc),
                                    se_client_malloc_usable_size, //MC_(malloc_usable_size),
                                    0);

    VG_(basic_tool_funcs)        (se_post_clo_init,
                                  se_instrument,
                                 se_fini);

    VG_(track_new_mem_mmap)    (se_handle_new_mmap);

    VG_(needs_client_requests) (se_handle_client_request);

    memspace_init();
}

VG_DETERMINE_INTERFACE_VERSION(se_pre_clo_init);