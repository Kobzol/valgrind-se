#ifndef VALGRIND_NET_H
#define VALGRIND_NET_H

#include "common.h"

typedef Int Socket;

Socket net_connect(const HChar* server_addr);

Int net_read(Socket socket, HChar* buffer, Int size);
Int net_write(Socket socket, HChar* buffer);

#endif //VALGRIND_NET_H
