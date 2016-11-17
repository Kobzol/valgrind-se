#ifndef VALGRIND_NET_H
#define VALGRIND_NET_H

#include "common.h"

typedef Int Socket;

typedef struct
{
    Bool ok;
    SizeT arg1;
    const char* msg;
} NetMessage;

#define MSG_CREATE_CONSTRAINT "CREATE_CONSTRAINT"

// global vars
Socket conn;

// init
void net_init(const HChar* server_addr);
Socket net_connect(const HChar* server_addr);

// communication
NetMessage net_msg(Socket socket, const HChar* message);

#endif //VALGRIND_NET_H
