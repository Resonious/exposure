#include "trace.h"
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

VALUE cTrace;
const unsigned int kSingleton = kClassSingleton | kModuleSingleton |
                                kObjectSingleton | kOtherSingleton;

static void handle_return_event(VALUE _tracepoint, trace_t *trace) {
    if (trace->stack_depth == 0) return;

#ifdef TRACY_ENABLE
    TracyCZoneCtx ctx = trace->tracy_ctx_stack[trace->stack_depth - 1];
    ___tracy_emit_zone_end(ctx);
    trace->stack_depth -= 1;
#endif
}

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
    rb_gc_mark(trace->current_file_name);
}

static void trace_free(void *data) {
    trace_t *trace = (trace_t*)data;

    if (trace->strings_table) {
        st_free_table(trace->strings_table);
    }

#ifdef TRACY_ENABLE
    while (trace->stack_depth > 0) {
        handle_return_event(Qnil, trace);
    }
#endif

    xfree(trace->tracy_ctx_stack);

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
        .dfree = trace_free,
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

    trace->strings_table = NULL;
    trace->stack_depth = 0;

    return result;
}


/*
 * =============
 * Trace methods
 * =============
 */

/*
static const char* get_event_name(rb_event_flag_t event) {
    switch (event) {
    case RUBY_EVENT_LINE:
        return "line";
    case RUBY_EVENT_CLASS:
        return "class";
    case RUBY_EVENT_END:
        return "end";
    case RUBY_EVENT_CALL:
        return "call";
    case RUBY_EVENT_RETURN:
        return "return";
    case RUBY_EVENT_B_CALL:
        return "b-call";
    case RUBY_EVENT_B_RETURN:
        return "b-return";
    case RUBY_EVENT_C_CALL:
        return "c-call";
    case RUBY_EVENT_C_RETURN:
        return "c-return";
    case RUBY_EVENT_THREAD_BEGIN:
        return "thread-begin";
    case RUBY_EVENT_THREAD_END:
        return "thread-end";
    case RUBY_EVENT_FIBER_SWITCH:
        return "fiber-switch";
    case RUBY_EVENT_RAISE:
        return "raise";
    case RUBY_INTERNAL_EVENT_NEWOBJ:
        return "newobj";
    default:
        return "unknown";
    }
}
*/

static const char *get_class_name(VALUE klass, unsigned int* flags) {
    VALUE attached;
    *flags = 0;

    if (klass == 0 || klass == Qnil) {
        return "nil";
    }

    if (BUILTIN_TYPE(klass) == T_CLASS && FL_TEST(klass, FL_SINGLETON)) {
        /* This is a class singleton. Something like MyClass.my_method */
        *flags = kClassSingleton;
        attached = rb_iv_get(klass, "__attached__");

        switch (BUILTIN_TYPE(attached)) {
        case T_MODULE:
            *flags = kModuleSingleton;
        case T_CLASS:
            klass = attached;
            break;
        default: break;
        }
    }

    return rb_class2name(klass);
}

static void handle_line_event(VALUE tracepoint, trace_t *trace) {
    rb_trace_arg_t *trace_arg;

    trace_arg = rb_tracearg_from_tracepoint(tracepoint);

    trace->current_file_name = rb_tracearg_path(trace_arg);
    trace->current_line_number = FIX2INT(rb_tracearg_lineno(trace_arg));
}

static void handle_call_event(VALUE tracepoint, trace_t *trace) {
    rb_trace_arg_t *trace_arg;
    VALUE fiber;

    VALUE source_file;
    int source_line;
    VALUE callee, klass;
    unsigned int class_flags;
    const char *class_name;
    const char *method_name_cstr;
    char method_sep;

    if (trace->stack_depth >= trace->stack_cap) {
        trace->stack_cap *= 2;
        trace->tracy_ctx_stack = xrealloc(trace->tracy_ctx_stack, trace->stack_cap * sizeof(TracyCZoneCtx));
    }

    fiber = rb_fiber_current();

    trace_arg = rb_tracearg_from_tracepoint(tracepoint);

    source_file    = trace->current_file_name;
    source_line    = trace->current_line_number;
    callee         = rb_tracearg_callee_id(trace_arg);
    klass          = rb_tracearg_defined_class(trace_arg);
    class_name     = get_class_name(klass, &class_flags);

    method_name_cstr = (callee != Qnil ? rb_id2name(SYM2ID(callee)) : "<none>");
    if (source_file == Qnil) source_file = rb_str_new_cstr("<none>");

    if (class_flags & kSingleton) method_sep = '.';
    else                          method_sep = '#';

#ifdef TRACY_ENABLE
    VALUE qualified_method = rb_sprintf("%s%c%s", class_name, method_sep, method_name_cstr);

    const uint64_t srcloc = ___tracy_alloc_srcloc(
        source_line,
        RSTRING_PTR(source_file), RSTRING_LEN(source_file),
        RSTRING_PTR(qualified_method), RSTRING_LEN(qualified_method)
    );

    TracyCZoneCtx ctx = ___tracy_emit_zone_begin_alloc(srcloc, 1);
    ___tracy_emit_zone_name(ctx, RSTRING_PTR(qualified_method), RSTRING_LEN(qualified_method));
    ___tracy_emit_zone_text(ctx, RSTRING_PTR(source_file), RSTRING_LEN(source_file));
    // TODO could color code the trace based on whether it is in a gem or not
    trace->tracy_ctx_stack[trace->stack_depth++] = ctx;
#endif

    //add_stringf(
    //    &trace->strings,
    //    entry->caller_file_start,
    //    entry->caller_file_len,
    //    "%s\n", trace->current_file_name
    //);

    //add_stringf(
    //    &trace->strings,
    //    entry->callee_file_start,
    //    entry->callee_file_len,
    //    "%s\n", source_file_cstr
    //);
}

/*
 * This is heavily "inspired" by ruby-prof
 * https://github.com/ruby-prof/ruby-prof/blob/master/ext/ruby_prof/rp_profile.c
 */
static void event_hook(VALUE tracepoint, void *data) {
    rb_trace_arg_t *trace_arg;
    rb_event_flag_t event;

    trace_arg = rb_tracearg_from_tracepoint(tracepoint);
    event     = rb_tracearg_event_flag(trace_arg);

    switch (event) {
    case RUBY_EVENT_LINE:
        handle_line_event(tracepoint, (trace_t *)data);
        break;

    case RUBY_EVENT_CALL:
    case RUBY_EVENT_C_CALL:
        handle_call_event(tracepoint, (trace_t *)data);
        break;

    case RUBY_EVENT_RETURN:
    case RUBY_EVENT_C_RETURN:
        handle_return_event(tracepoint, (trace_t *)data);
        break;
    }
}


/*
 * =====================
 * Exported Ruby methods
 * =====================
 */

static VALUE trace_initialize(VALUE self) {
    trace_t *trace = RTYPEDDATA_DATA(self);

    trace->current_file_name = rb_str_new_cstr("<none>");
    trace->current_line_number = -1;

    trace->strings_table = st_init_strtable_with_size(4096);

    trace->stack_depth = 0;
    trace->stack_cap = 1024;
    trace->tracy_ctx_stack = xmalloc(trace->stack_cap * sizeof(TracyCZoneCtx));

    return self;
}

static VALUE trace_tracepoint(VALUE self) {
    trace_t *trace = RTYPEDDATA_DATA(self);

    if (trace->tracepoint == Qnil) {
        trace->tracepoint = rb_tracepoint_new(
            Qnil,
            RUBY_EVENT_CALL | RUBY_EVENT_RETURN |
            RUBY_EVENT_C_CALL | RUBY_EVENT_C_RETURN |
            RUBY_EVENT_LINE,
            event_hook, (void*)trace
        );
    }

    return trace->tracepoint;
}

static VALUE trace_start(VALUE self) {
#ifdef TRACY_FIBERS
    VALUE fiber = rb_fiber_current();
    VALUE fiber_name = rb_funcall(fiber, rb_intern("inspect"), 0);
    const char *fiber_name_cstr = RSTRING_PTR(fiber_name);
    ___tracy_fiber_enter(fiber_name_cstr);
#endif

    VALUE tracepoint = trace_tracepoint(self);
    rb_tracepoint_enable(tracepoint);
    return Qnil;
}

static VALUE trace_stop(VALUE self) {
    VALUE tracepoint = trace_tracepoint(self);
    VALUE disabled = rb_tracepoint_disable(tracepoint);
#ifdef TRACY_ENABLE
    trace_t *trace = RTYPEDDATA_DATA(self);
    if (disabled == Qtrue) {
        while (trace->stack_depth > 0) {
            handle_return_event(Qnil, trace);
        }
    }
#endif
    return Qnil;
}

/*
 * ================
 * Class definition
 * ================
 */

void ft_init_trace(void) {
    cTrace = rb_define_class_under(mFasttrace, "Trace", rb_cObject);
    rb_define_alloc_func(cTrace, trace_allocate);

    rb_define_method(cTrace, "initialize", trace_initialize, 0);
    rb_define_method(cTrace, "tracepoint", trace_tracepoint, 0);
    rb_define_method(cTrace, "start", trace_start, 0);
    rb_define_method(cTrace, "stop", trace_stop, 0);
}
