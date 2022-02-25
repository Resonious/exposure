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

/*
 * Taken straight-up from ruby-prof...
 * https://github.com/ruby-prof/ruby-prof/blob/master/ext/ruby_prof/rp_method.c
 */
static VALUE resolve_klass(VALUE klass, unsigned int* klass_flags) {
    VALUE result = klass;

    if (klass == 0 || klass == Qnil)
    {
        result = Qnil;
    }
    else if (BUILTIN_TYPE(klass) == T_CLASS && FL_TEST(klass, FL_SINGLETON))
    {
        /* We have come across a singleton object. First
           figure out what it is attached to.*/
        VALUE attached = rb_iv_get(klass, "__attached__");

        /* Is this a singleton class acting as a metaclass? */
        if (BUILTIN_TYPE(attached) == T_CLASS)
        {
            *klass_flags |= kClassSingleton;
            result = attached;
        }
        /* Is this for singleton methods on a module? */
        else if (BUILTIN_TYPE(attached) == T_MODULE)
        {
            *klass_flags |= kModuleSingleton;
            result = attached;
        }
        /* Is this for singleton methods on an object? */
        else if (BUILTIN_TYPE(attached) == T_OBJECT)
        {
            *klass_flags |= kObjectSingleton;
            result = rb_class_superclass(klass);
        }
        /* Ok, this could be other things like an array made put onto
           a singleton object (yeah, it happens, see the singleton
           objects test case). */
        else
        {
            *klass_flags |= kOtherSingleton;
            result = klass;
        }
    }
    /* Is this an include for a module?  If so get the actual
        module class since we want to combine all profiling
        results for that module. */
    else if (BUILTIN_TYPE(klass) == T_ICLASS)
    {
        unsigned int dummy;
        *klass_flags |= kModuleIncludee;
        result = resolve_klass(RBASIC(klass)->klass, &dummy);
    }
    return result;
}

const char *resolve_klass_name(VALUE klass, unsigned int klass_flags)
{
    VALUE val;

    if (klass == Qnil)
    {
        return "[global]";
    }
    else if (klass_flags & kOtherSingleton)
    {
        val = rb_any_to_s(klass);
        return StringValuePtr(val);
    }
    else
    {
        val = rb_class_name(klass);
        return StringValuePtr(val);
    }
}

static const char* get_event_name(rb_event_flag_t event)
{
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

/*
 * This is heavily "inspired" by ruby-prof
 * https://github.com/ruby-prof/ruby-prof/blob/master/ext/ruby_prof/rp_profile.c
 */
static void event_hook(VALUE tracepoint, void *data) {
    trace_t *trace;
    rb_trace_arg_t *trace_arg;
    rb_event_flag_t event;
    unsigned int klass_flags;
    VALUE fiber;

    const char *event_name;
    VALUE source_file;
    int source_line;
    VALUE callee, klass, resolved_klass;
    const char *class_name;

    trace = (trace_t*)data;
    fiber = rb_fiber_current();

    trace_arg = rb_tracearg_from_tracepoint(tracepoint);
    event     = rb_tracearg_event_flag(trace_arg);

    event_name     = get_event_name(event);
    source_file    = rb_tracearg_path(trace_arg);
    source_line    = FIX2INT(rb_tracearg_lineno(trace_arg));
    callee         = rb_tracearg_callee_id(trace_arg);
    klass          = rb_tracearg_defined_class(trace_arg);
    resolved_klass = resolve_klass(klass, &klass_flags);
    class_name     = resolve_klass_name(resolved_klass, klass_flags);

    const char* method_name_cstr = (callee != Qnil ? rb_id2name(SYM2ID(callee)) : "");
    const char* source_file_cstr = (source_file != Qnil ? StringValuePtr(source_file) : "");

    fprintf(
        trace->trace_file, "%2lu:%2f %-8s %s#%s    %s:%2d\n",
        FIX2ULONG(fiber), measure_wall_time(),
        event_name, class_name, method_name_cstr, source_file_cstr, source_line
    );
}


/*
 * =====================
 * Exported Ruby methods
 * =====================
 */

static VALUE trace_initialize(VALUE self, VALUE trace_file_name) {
    trace_t *trace = RTYPEDDATA_DATA(self);
    const char *trace_file_name_cstr = StringValuePtr(trace_file_name);

    trace->trace_file = fopen(trace_file_name_cstr, "w");
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


/*
 * ================
 * Class definition
 * ================
 */

void ft_init_trace(void) {
    cTrace = rb_define_class_under(mFasttrace, "Trace", rb_cObject);
    rb_define_alloc_func(cTrace, trace_allocate);

    rb_define_method(cTrace, "initialize", trace_initialize, 1);
    rb_define_method(cTrace, "tracepoint", trace_tracepoint, 0);
}
