#ifndef EXPOSURE_TRACE_H
#define EXPOSURE_TRACE_H 1

#include "exposure.h"
#include "measure.h"
#include <stdio.h>

#define METHOD_KEY_LEN 512
#define FRAMES_MAX 4096

typedef struct trace_frame_t {
    VALUE file_name;
    int line_number;
    // If this is 0, then the frame is a "leaf call" and we will display it to the user.
    int calls;
    int is_in_root;
    char method_key[METHOD_KEY_LEN];
} trace_frame_t;

typedef struct trace_stack_t {
    char *name;

    trace_frame_t frames[FRAMES_MAX];
    int frames_count;
    int new_call;
    VALUE callee;
    VALUE klass;

    VALUE current_file_name;
    int current_line_number;
} trace_stack_t;

typedef struct trace_t {
    VALUE tracepoint;

    VALUE project_root;
    VALUE path_blocklist;

    st_table *fibers_table;
} trace_t;

void ft_init_trace(void);

#endif /* EXPOSURE_TRACE_H */
