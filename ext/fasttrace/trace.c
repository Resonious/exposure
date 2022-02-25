#include "trace.h"

VALUE cTrace;

static VALUE trace_start(VALUE self, VALUE trace_file_name) {
    const char *trace_file_name_cstr = StringValuePtr(trace_file_name);
    printf("Hohoho %s\n", trace_file_name_cstr);
    return Qnil;
}

void ft_init_trace(void) {
    cTrace = rb_define_class_under(mFasttrace, "Trace", rb_cObject);

    rb_define_method(cTrace, "start", trace_start, 1);
}
