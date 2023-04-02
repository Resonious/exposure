#ifndef FASTTRACE_TRACE_H
#define FASTTRACE_TRACE_H 1

#include "fasttrace.h"
#include "measure.h"
#include <stdio.h>
#include "../../tracy/public/tracy/TracyC.h"

typedef struct trace_t {
    VALUE tracepoint;

    st_table *fibers_table;

    // TODO: track these per fiber
    size_t stack_depth, stack_cap;
    TracyCZoneCtx *tracy_ctx_stack;

    VALUE current_file_name;
    int current_line_number;
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
