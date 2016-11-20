
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

#include "pub_tool_options.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcprint.h"
#include "valgrind.h"

#include "../VEX/pub/libvex_ir.h"

#include "client/se_client.h"
#include "src/memory.h"
#include "src/instrument.h"
#include "src/net.h"
#include "src/state.h"
#include "src/symbolic.h"
#include "src/expr.h"

static State* state = NULL;
static Bool se_handle_client_request (ThreadId tid, UWord* args, UWord* ret)
{
    if (!VG_IS_TOOL_USERREQ('S','E', args[0]))
    {
        return False;
    }

    SEArgType* requestArgs = (SEArgType*) args[2];
    SEArgType argsSize = (SEArgType) args[3];

    switch (args[1])
    {
        case VG_USERREQ__SE_SAVE_STATE:
        {
            state = state_save_current();
            break;
        }
        case VG_USERREQ__SE_RESTORE_STATE:
        {
            if (state == NULL) break;

            state_restore(state);
            state = NULL;
            break;
        }
        case VG_USERREQ__SE_MAKE_SYMBOLIC:
        {
            Addr a = (Addr) requestArgs[0];
            SizeT size = (Addr) requestArgs[1];

            set_address_range_sym(a, size, SYM_SYMBOLIC);

            char buffer[512];
            VG_(sprintf)(buffer, "%s 0 %lu", MSG_CREATE_CONSTRAINT, size);

            NetMessage msg = net_msg(conn, buffer);
            tl_assert(msg.ok);

            sym_insert(a, msg.arg1);

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

static void se_fini(Int exitcode)
{
}

/// cmd options
static Bool connect_to_server = False;

static Bool se_process_cmd_line_option(const HChar* arg)
{
    if (VG_BOOL_CLO(arg, "--connect", connect_to_server))
    {
        return True;
    }

    return False;
}

static void se_print_usage(void)
{
}

static void se_print_debug_usage(void)
{

}

static void se_post_clo_init(void)
{
    if (connect_to_server)
    {
        net_init("127.0.0.1:5555");
    }
}

static void se_pre_clo_init(void)
{
    VG_(details_name)            ("Segrind test");
    VG_(details_version)         (NULL);
    VG_(details_description)     ("the symbolic Valgrind tool");
    VG_(details_copyright_author)(
      "Copyright (C) 2002-2015, and GNU GPL'd, by Nicholas Nethercote.");
    VG_(details_bug_reports_to)  (VG_BUGS_TO);

    VG_(details_avg_translation_sizeB) (275);

    VG_(needs_command_line_options)(se_process_cmd_line_option,
                                    se_print_usage,
                                    se_print_debug_usage);

    //VG_(needs_syscall_wrapper)(syscall_handle_pre, syscall_handle_post);

    VG_(needs_malloc_replacement)  (se_handle_malloc,
                                    se_handle_malloc, //MC_(__builtin_new),
                                    se_handle_malloc, //MC_(__builtin_vec_new),
                                    se_handle_memalign, //MC_(memalign),
                                    se_handle_calloc, //MC_(calloc),
                                    se_handle_free, //MC_(free),
                                    se_handle_free, //MC_(__builtin_delete),
                                    se_handle_free, //MC_(__builtin_vec_delete),
                                    se_handle_realloc, //MC_(realloc),
                                    se_handle_malloc_usable_size, //MC_(malloc_usable_size),
                                    0);

    VG_(basic_tool_funcs)        (se_post_clo_init,
                                  se_instrument,
                                  se_fini);

    // mmap
    VG_(track_new_mem_mmap)    (se_handle_mmap);
    VG_(track_new_mem_startup) (se_handle_mstartup);
    VG_(track_change_mem_mprotect) (se_handle_mprotect);
    VG_(track_copy_mem_remap)      (se_handle_mremap);
    VG_(track_die_mem_stack_signal)(se_handle_munmap);
    VG_(track_die_mem_brk)         (se_handle_munmap);
    VG_(track_die_mem_munmap)      (se_handle_munmap);

    // stack alloc
    VG_(track_new_mem_stack_signal) (se_handle_stack_signal);
    VG_(track_new_mem_stack) (se_handle_stack_new);
    VG_(track_die_mem_stack) (se_handle_stack_die);
    VG_(track_ban_mem_stack)       (se_handle_stack_ban);

    VG_(track_post_mem_write)      (se_handle_post_mem_write);

    VG_(needs_client_requests) (se_handle_client_request);

    memspace_init();
    expr_init();
}

VG_DETERMINE_INTERFACE_VERSION(se_pre_clo_init);