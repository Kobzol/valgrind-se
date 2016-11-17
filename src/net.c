#include "net.h"

Socket conn = -1;

static char readBuffer[4096] = { 0 };
static char responseBuffer[4096] = { 0 };
static SizeT readBufferIndex = 0;

static Int net_read(Socket socket, Int size)
{
    SizeT readSize = VG_(read)(socket, readBuffer + readBufferIndex, size);
    readBufferIndex += readSize;
    return readSize;
}
static Int net_readline(Socket socket)
{
    SizeT index = readBufferIndex;
    Int size = net_read(socket, 64);
    Int end = index + size;
    for (int i = index; i < end; i++)
    {
        if (readBuffer[i] == '\n')
        {
            VG_(memcpy)(responseBuffer, readBuffer, i);
            responseBuffer[i] = 0;

            VG_(memmove)(readBuffer + i + 1, readBuffer, end - i - 1);
            readBufferIndex = end - i - 1;

            return i;
        }
    }

    return net_readline(socket);
}
static Int net_write(Socket socket, const HChar* buffer)
{
    return VG_(write)(socket, buffer, VG_(strlen)(buffer));
}
static Int net_writeline(Socket socket, const HChar* buffer)
{
    Int size = net_write(socket, buffer);
    size += net_write(socket, "\n");

    return size;
}

static NetMessage parse_msg(HChar* message)
{
    NetMessage msg;
    msg.ok = message[0] == '1';
    msg.msg = (const char*) message;

    Int argCount = message[2];
    Int argStart = 4;

    char* argStr = VG_(strtok)(message + argStart, " ");
    msg.arg1 = VG_(strtoll10)(argStr, NULL);

    return msg;
}

void net_init(const HChar* server_addr)
{
    conn = net_connect(server_addr);
}
Socket net_connect(const HChar* server_addr)
{
    Socket s = VG_(connect_via_socket(server_addr));

    if (s == -1)
    {
        VG_(fmsg)("Invalid server address '%s'\n", server_addr);
        VG_(exit)(1);
    }
    if (s == -2)
    {
        VG_(umsg)("failed to connect to server '%s'.\n", server_addr);
        VG_(exit)(1);
    }
    tl_assert(s > 0);
    return s;
}

NetMessage net_msg(Socket socket, const HChar* message)
{
    net_writeline(socket, message);
    SizeT size = net_readline(socket);

    if (size > 0)
    {
        return parse_msg(responseBuffer);
    }
    else
    {
        NetMessage result = { .ok = False};
        return result;
    }
}
