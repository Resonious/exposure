#include "trace.h"
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

static VALUE sname;
VALUE cTrace;
const unsigned int kSingleton = kClassSingleton | kModuleSingleton |
                                kObjectSingleton | kOtherSingleton;

static tracy_stack_t *stack_for_fiber(trace_t *trace, VALUE fiber);

static void pop_stack(tracy_stack_t *stack) {
    if (stack->depth == 0) return;
#ifdef TRACY_ENABLE
    TracyCZoneCtx ctx = stack->ctx_stack[stack->depth - 1];
    ___tracy_emit_zone_end(ctx);
    stack->depth -= 1;
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

static int mark_traced_fiber(st_data_t key, st_data_t _value, st_data_t _args) {
    VALUE fiber = (VALUE)key;
    rb_gc_mark(fiber);
    return ST_CONTINUE;
}

static int free_traced_fiber(st_data_t key, st_data_t value, st_data_t _args) {
    tracy_stack_t *stack = (tracy_stack_t *)value;

    while (stack->depth > 0) {
        pop_stack(stack);
    }

    xfree(stack);
    return ST_CONTINUE;
}

static void trace_mark(void *data) {
    trace_t *trace = (trace_t*)data;
    rb_gc_mark(trace->tracepoint);
    rb_gc_mark(trace->current_file_name);
    rb_gc_mark(trace->last_fiber);
    rb_gc_mark(trace->project_root);
    rb_gc_mark(trace->current_method_name);
    st_foreach(trace->fibers_table, mark_traced_fiber, 0);
}

static void trace_free(void *data) {
    trace_t *trace = (trace_t*)data;

    if (trace->fibers_table) {
        // free all of the tracy_stack_t inside of trace->fibers_table st_table.
        st_foreach(trace->fibers_table, free_traced_fiber, 0);
        st_free_table(trace->fibers_table);
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
    trace->last_fiber = Qnil;
    trace->project_root = Qnil;
    trace->current_method_name = Qnil;

    trace->fibers_table = NULL;

    return result;
}


/*
 * =============
 * Trace methods
 * =============
 */

static int is_in_project_root(trace_t *trace) {
    if (trace->project_root == Qnil) return 1;

    VALUE current_file_name_rstr = trace->current_file_name;
    if (current_file_name_rstr == Qnil) return 1;

    const char *current_file_name = StringValuePtr(current_file_name_rstr);
    const char *project_root = StringValuePtr(trace->project_root);

    const char *c1 = project_root;
    const char *c2 = current_file_name;
    while (*c1 && *c2) {
        if (*c1 != *c2) return 0;
        c1++;
        c2++;
    }
    return 1;
}

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

static void sync_tracy_fiber(trace_t *trace, tracy_stack_t *stack) {
    VALUE fiber = rb_fiber_current();

#ifdef TRACY_FIBERS
    if (!rb_eql(fiber, trace->last_fiber)) {
        ___tracy_fiber_enter(stack->name);
        trace->last_fiber = fiber;
    }
#endif
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
    tracy_stack_t *stack;

    VALUE source_file;
    int source_line;
    VALUE callee, klass;
    unsigned int class_flags;
    const char *class_name;
    const char *method_name_cstr;
    const char *current_file_name_cstr;
    char method_sep;
    rb_event_flag_t event;
    const char *event_name;

    fiber = rb_fiber_current();
    stack = stack_for_fiber(trace, fiber);

    sync_tracy_fiber(trace, stack);

    trace_arg = rb_tracearg_from_tracepoint(tracepoint);

    source_file    = rb_tracearg_path(trace_arg);
    source_line    = FIX2INT(rb_tracearg_lineno(trace_arg));
    callee         = rb_tracearg_callee_id(trace_arg);
    klass          = rb_tracearg_defined_class(trace_arg);
    class_name     = get_class_name(klass, &class_flags);
    event          = rb_tracearg_event_flag(trace_arg);
    event_name     = get_event_name(event);

    method_name_cstr = (callee != Qnil ? rb_id2name(SYM2ID(callee)) : "<none>");
    if (source_file == Qnil) source_file = rb_str_new_cstr("<none>");

    if (trace->current_file_name == Qnil) {
        current_file_name_cstr = "<unknown>";
    } else {
        current_file_name_cstr = StringValueCStr(trace->current_file_name);
    }

    if (class_flags & kSingleton) method_sep = '.';
    else                          method_sep = '#';

    VALUE qualified_method = rb_sprintf("%s%c%s", class_name, method_sep, method_name_cstr);

    VALUE extra_info = rb_sprintf(
        "Event type: %s\nCalled from: %s:%d",
        event_name,
        current_file_name_cstr, trace->current_line_number
    );

    trace->current_method_name = qualified_method;

#ifdef TRACY_ENABLE
    const uint64_t srcloc = ___tracy_alloc_srcloc(
        source_line,
        RSTRING_PTR(source_file), RSTRING_LEN(source_file),
        RSTRING_PTR(qualified_method), RSTRING_LEN(qualified_method)
    );

    TracyCZoneCtx ctx = ___tracy_emit_zone_begin_alloc(srcloc, 1);
    ___tracy_emit_zone_name(ctx, RSTRING_PTR(qualified_method), RSTRING_LEN(qualified_method));
    ___tracy_emit_zone_text(ctx, RSTRING_PTR(extra_info), RSTRING_LEN(extra_info));

    if (is_in_project_root(trace)) {
        ___tracy_emit_zone_color(ctx, 0x2f4b8c);
    } else {
        ___tracy_emit_zone_color(ctx, 0xb26258);
    }

    stack->ctx_stack[stack->depth++] = ctx;
#endif
}

static void handle_return_event(VALUE _tracepoint, trace_t *trace) {
    VALUE fiber;
    tracy_stack_t *stack;

    fiber = rb_fiber_current();
    stack = stack_for_fiber(trace, fiber);

    sync_tracy_fiber(trace, stack);

    pop_stack(stack);
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

static void stack_init(tracy_stack_t *stack, VALUE fiber) {
    stack->depth = 0;
    stack->cap = 1024;
    stack->ctx_stack = xmalloc(stack->cap * sizeof(TracyCZoneCtx));

    VALUE thread = rb_thread_current();
    VALUE thread_name = rb_funcall(thread, sname, 0);
    const char *thread_name_cstr = thread_name != Qnil ? StringValueCStr(thread_name) : "Fiber";

    sprintf(stack->name, "Ruby %s %i", thread_name_cstr, FIX2INT(rb_obj_id(fiber)));
}

// Boilerplate for st_table
static int rb_value_compare(st_data_t a, st_data_t b) {
    VALUE va = (VALUE)a;
    VALUE vb = (VALUE)b;

    return rb_eql(va, vb);
}

// Boilerplate for st_table
static st_index_t rb_value_hash(st_data_t a) {
    VALUE va = (VALUE)a;
    return rb_hash(va);
}

static struct st_hash_type fibers_table_type;

static tracy_stack_t *stack_for_fiber(trace_t *trace, VALUE fiber) {
    tracy_stack_t *stack;
    st_data_t data;

    if (!trace->fibers_table) {
        trace->fibers_table = st_init_table_with_size(&fibers_table_type, 16);
    }

    if (st_lookup(trace->fibers_table, fiber, &data)) {
        stack = (tracy_stack_t *)data;
    } else {
        stack = xmalloc(sizeof(tracy_stack_t));
        stack_init(stack, fiber);
        st_insert(trace->fibers_table, (st_data_t)fiber, (st_data_t)stack);
    }

    return stack;
}


/*
 * =====================
 * Exported Ruby methods
 * =====================
 */

static VALUE trace_initialize(VALUE self, VALUE project_root) {
    trace_t *trace = RTYPEDDATA_DATA(self);

    trace->current_file_name = rb_str_new_cstr("<none>");
    trace->current_line_number = -1;

    trace->fibers_table = NULL;
    trace->project_root = project_root;

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
    VALUE tracepoint = trace_tracepoint(self);
    rb_tracepoint_enable(tracepoint);

    return Qnil;
}

static VALUE trace_stop(VALUE self) {
    VALUE tracepoint = trace_tracepoint(self);
    VALUE disabled = rb_tracepoint_disable(tracepoint);
    trace_t *trace = RTYPEDDATA_DATA(self);
    if (disabled == Qtrue && trace->fibers_table) {
        st_foreach(trace->fibers_table, free_traced_fiber, 0);
        st_free_table(trace->fibers_table);
        trace->fibers_table = NULL;
    }
    return Qnil;
}

static VALUE trace_frame(VALUE self, VALUE frame_name) {
    if (!rb_block_given_p()) {
        rb_raise(rb_eArgError, "A block is required");
    }

    trace_t *trace = RTYPEDDATA_DATA(self);
    VALUE fiber = rb_fiber_current();
    tracy_stack_t *stack = stack_for_fiber(trace, fiber);

    sync_tracy_fiber(trace, stack);

    if (frame_name == Qnil) frame_name = trace->current_method_name;
    const char *frame_name_cstr = StringValueCStr(frame_name);

#ifdef TRACY_ENABLE
    ___tracy_emit_frame_mark_start(frame_name_cstr);
#endif

    VALUE result = rb_yield(Qnil);

#ifdef TRACY_ENABLE
    ___tracy_emit_frame_mark_end(frame_name_cstr);
#endif

    return result;
}

/*
 * ================
 * Class definition
 * ================
 */

void ft_init_trace(void) {
    fibers_table_type.compare = rb_value_compare;
    fibers_table_type.hash = rb_value_hash;

    sname = rb_intern("name");

    cTrace = rb_define_class_under(mTracyrb, "Trace", rb_cObject);
    rb_define_alloc_func(cTrace, trace_allocate);

    rb_define_method(cTrace, "initialize", trace_initialize, 1);
    rb_define_method(cTrace, "tracepoint", trace_tracepoint, 0);
    rb_define_method(cTrace, "start", trace_start, 0);
    rb_define_method(cTrace, "stop", trace_stop, 0);
    rb_define_method(cTrace, "frame", trace_frame, 1);
}
