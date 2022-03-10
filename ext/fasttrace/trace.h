#ifndef FASTTRACE_TRACE_H
#define FASTTRACE_TRACE_H 1

#include "fasttrace.h"
#include "measure.h"
#include <stdio.h>

typedef struct trace_file_t {
    /* file handle, should be rw+ */
    FILE *file;
    /* mmapped data ptr */
    void *data;

    /*
     * Current offset from the start of the file.
     */
    size_t i;
    /*
     * Offset of data ptr relative to start of file.
     */
    size_t offset;
    /*
     * Total size of file.
     */
    size_t len;
} trace_file_t;

typedef struct trace_t {
    VALUE tracepoint;

    trace_file_t header;
    trace_file_t strings;

    st_table *strings_table;

    VALUE current_file_name;
    int current_line_number;

    int running;
} trace_t;

enum {
    kModuleIncludee = 0x1,  // Included in module
    kClassSingleton = 0x2,  // Singleton of a class
    kModuleSingleton = 0x4, // Singleton of a module
    kObjectSingleton = 0x8, // Singleton of an object
    kOtherSingleton = 0x10  // Singleton of unkown object
};
extern const unsigned int kSingleton;

void ft_init_trace(void);

#endif /* FASTTRACE_TRACE_H */
