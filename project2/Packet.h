#ifndef __PACKET__
#define __PACKET__

#include "PacketHeader.h"

#define DATASIZE 1456

struct Packet
{
    PacketHeader header;
    char buffer[DATASIZE];
};

#endif
