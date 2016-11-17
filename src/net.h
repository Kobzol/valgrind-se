#ifndef VALGRIND_NET_H
#define VALGRIND_NET_H

#include "common.h"

typedef Int Socket;

Socket conn;

void net_init(const HChar* server_addr);
Socket net_connect(const HChar* server_addr);

const char* net_msg(Socket socket, const HChar* message);

#endif //VALGRIND_NET_H
