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
    filedict_t blocks;

    VALUE project_root;

    VALUE current_file_name;
    int current_line_number;
} trace_t;

void ft_init_trace(void);

#endif /* FASTTRACE_TRACE_H */
