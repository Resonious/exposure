#ifndef FASTTRACE_TRACE_H
#define FASTTRACE_TRACE_H 1

#include "fasttrace.h"
#include "measure.h"
#include <stdio.h>

#define FILEDICT_IMPL 1
#include "../../filedict/filedict.h"
#undef FILEDICT_IMPL

typedef struct trace_t {
    VALUE tracepoint;

    filedict_t returns;
    filedict_t locals;

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
