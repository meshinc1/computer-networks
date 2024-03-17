#ifndef __CHUNK__
#define __CHUNK__

#define CHUNKSIZE 512000

struct Chunk
{
    unsigned int length;
    char buffer[CHUNKSIZE];
};

#endif
