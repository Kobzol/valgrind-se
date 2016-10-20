#include "syscall.h"

SyscallHandler PreHandlers[1024] = {0};
SyscallHandler PostHandlers[1024] = {0};

void syscall_init(void)
{
    PostHandlers[__NR_read] = handle_read;
}

void syscall_handle_pre(ThreadId tid, UInt syscallno,
                 UWord* args, UInt nArgs)
{
    if (PreHandlers[syscallno] != NULL)
    {
        SysRes res;
        PreHandlers[syscallno](tid, args, nArgs, res);
    }
}
void syscall_handle_post(ThreadId tid, UInt syscallno,
                  UWord* args, UInt nArgs, SysRes res)
{
    if (PostHandlers[syscallno] != NULL)
    {
        PostHandlers[syscallno](tid, args, nArgs, res);
    }
}

void handle_read(ThreadId tid, UWord* args, UInt nArgs, SysRes res)
{
    int fd = args[0];
    void* buf = (void*) args[1];
    int count = args[2];
    int result = res._val;

    //PRINT(LOG_DEBUG, "Read: fd=%d, addr=%p, count=%d, result=%d\n", fd, buf, count, result);
}