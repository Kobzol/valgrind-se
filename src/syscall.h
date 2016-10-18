#ifndef VALGRIND_SYSCALL_H_H
#define VALGRIND_SYSCALL_H_H

#include <linux/unistd.h>

#include "pub_tool_basics.h"

#include "common.h"

typedef void (*SyscallHandler)(ThreadId, UWord*, UInt, SysRes res);

extern SyscallHandler PreHandlers[1024];
extern SyscallHandler PostHandlers[1024];

void syscall_init(void);

void syscall_handle_pre(ThreadId tid, UInt syscallno,
                        UWord* args, UInt nArgs);
void syscall_handle_post(ThreadId tid, UInt syscallno,
                         UWord* args, UInt nArgs, SysRes res);

void handle_read(ThreadId tid, UWord* args, UInt nArgs, SysRes res);

#endif //VALGRIND_SYSCALL_H_H
