#include "net.h"

Socket net_connect(const HChar *server_addr)
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

Int net_read(Socket socket, HChar* buffer, Int size)
{
    return VG_(read)(socket, buffer, size);
}
Int net_write(Socket socket, HChar* buffer)
{
    return VG_(write)(socket, buffer, VG_(strlen)(buffer));
}
