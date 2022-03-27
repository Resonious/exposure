#ifndef FASTTRACE_TRACE_H
#define FASTTRACE_TRACE_H 1

#include "fasttrace.h"
#include "measure.h"
#include <stdio.h>

#define ENTRY_TYPE_CALL 1
#define ENTRY_TYPE_RETURN 2

/*
 * NOTE: this struct needs to have a byte size that is a power of 2.
 */
typedef struct trace_entry_t {
    unsigned long long type : 32;

    unsigned long long method_name_start : 64;
    unsigned long long method_name_len : 32;

    unsigned long long caller_file_start : 64;
    unsigned long long caller_file_len : 32;
    unsigned long long caller_line_number : 32;

    unsigned long long callee_file_start : 64;
    unsigned long long callee_file_len : 32;
    unsigned long long callee_line_number : 32;

    double timestamp;

    /* TODO: 8 more bytes... */
} __attribute__ ((__packed__)) trace_entry_t;

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

    trace_file_t entries;
    trace_file_t returns;
    trace_file_t locals;

    st_table *strings_table;

    VALUE project_root;

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
