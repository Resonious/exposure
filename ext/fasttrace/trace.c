#include "trace.h"

VALUE cTrace;


/*
 * ==============================
 * Custom allocation / GC support
 * ==============================
 *
 * Just like much of this whole file, this is largely pulled from ruby-prof
 * https://github.com/ruby-prof/ruby-prof/blob/master/ext/ruby_prof/rp_profile.c
 *
 * This stuff is necessary in order for GC to work properly when we hold
 * references to Ruby objects in C code.
 */

static void trace_mark(void *data) {
    trace_t *trace = (trace_t*)data;
    rb_gc_mark(trace->tracepoint);
}

static void trace_ruby_gc_free(void *data) {
    trace_t *trace = (trace_t*)data;

    if (trace->trace_file) {
        fclose(trace->trace_file);
    }

    xfree(trace);
}

size_t trace_size(const void* _data) {
    return sizeof(trace_t);
}

static const rb_data_type_t trace_type =
{
    .wrap_struct_name = "Trace",
    .function =
    {
        .dmark = trace_mark,
        .dfree = trace_ruby_gc_free,
        .dsize = trace_size,
    },
    .data = NULL,
    .flags = RUBY_TYPED_FREE_IMMEDIATELY
};

static VALUE trace_allocate(VALUE klass) {
    VALUE result;
    trace_t* trace;

    result = TypedData_Make_Struct(klass, trace_t, &trace_type, trace);
    trace->tracepoint = Qnil;
    trace->trace_file = NULL;
    trace->running = 0;

    return result;
}


/*
 * =============
 * Trace methods
 * =============
 */

/* TODO */
static void event_hook(VALUE tracepoint, void *data) {
    printf("hook ");
}


/*
 * =====================
 * Exported Ruby methods
 * =====================
 */

static VALUE trace_start(VALUE self, VALUE trace_file_name) {
    const char *trace_file_name_cstr = StringValuePtr(trace_file_name);
    trace_t *trace = RTYPEDDATA_DATA(self);

    trace->trace_file = fopen(trace_file_name_cstr, "w");

    trace->tracepoint = rb_tracepoint_new(
        Qnil,
        RUBY_EVENT_CALL | RUBY_EVENT_RETURN |
        RUBY_EVENT_C_CALL | RUBY_EVENT_C_RETURN |
        RUBY_EVENT_LINE,
        event_hook, (void*)trace
    );

    return trace->tracepoint;
}


/*
 * ================
 * Class definition
 * ================
 */

void ft_init_trace(void) {
    cTrace = rb_define_class_under(mFasttrace, "Trace", rb_cObject);
    rb_define_alloc_func(cTrace, trace_allocate);

    rb_define_method(cTrace, "start", trace_start, 1);
}
