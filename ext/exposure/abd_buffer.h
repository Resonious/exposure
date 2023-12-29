#ifndef _ABD_BUFFER
#define _ABD_BUFFER

typedef struct AbdBuffer {
    int pos;
    int capacity;
    unsigned char* bytes;
} AbdBuffer;

#endif // _ABD_BUFFER