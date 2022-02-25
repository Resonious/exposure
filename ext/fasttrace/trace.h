#ifndef FASTTRACE_TRACE_H
#define FASTTRACE_TRACE_H 1

#include "fasttrace.h"
#include <stdio.h>

typedef struct trace_t {
    VALUE tracepoint;
    FILE *trace_file;
    int running;
} trace_t;

void ft_init_trace(void);

#endif /* FASTTRACE_TRACE_H */
