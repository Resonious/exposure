#include "trace.h"
#include "ruby/debug.h"
#include "ruby/internal/event.h"
#include "ruby/internal/intern/string.h"
#include "ruby/internal/value_type.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>

ID id_local_variables;
ID id_local_variable_get;
VALUE cTrace;

static size_t PAGE_SIZE;
#define IDENTIFIER_MAX_SIZE (512 * 2)


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
    rb_gc_mark(trace->project_root);
    rb_gc_mark(trace->current_file_name);

    for (int i = 0; i < trace->frames_count; ++i) {
        rb_gc_mark(trace->frames[i].file_name);
    }
}

static void trace_free(void *data) {
    trace_t *trace = (trace_t*)data;

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
    trace->project_root = Qnil;
    trace->frames_count = 0;

    return result;
}


/*
 * =============
 * Trace methods
 * =============
 */

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

/*
 * Straight from ruby-prof... then modified
 */
static VALUE
figure_singleton_name(VALUE klass)
{
    VALUE attached, super;
    VALUE result = Qnil;

    /* We have come across a singleton object. First
       figure out what it is attached to.*/
    attached = rb_iv_get(klass, "__attached__");

    /* Is this a singleton class acting as a metaclass? */
    if (BUILTIN_TYPE(attached) == T_CLASS)
    {
        result = rb_class_name(attached);
    }

    /* Is this for singleton methods on a module? */
    else if (BUILTIN_TYPE(attached) == T_MODULE)
    {
        result = rb_class_name(attached);
    }

    /* Is this for singleton methods on an object? */
    else if (BUILTIN_TYPE(attached) == T_OBJECT)
    {
        /* Make sure to get the super class so that we don't
           mistakenly grab a T_ICLASS which would lead to
           unknown method errors. */
        super = rb_class_superclass(klass);
        result = rb_class_name(super);
    }

    /* Ok, this could be other things like an array made put onto
       a singleton object (yeah, it happens, see the singleton
       objects test case). */
    else
    {
        result = rb_any_to_s(klass);
    }

    return result;
}

static VALUE
klass_name(VALUE klass)
{
    VALUE result = Qnil;

    if (klass == 0 || klass == Qnil)
    {
        result = rb_str_new2("[global]");
    }
    else if (BUILTIN_TYPE(klass) == T_MODULE)
    {
        result = rb_class_name(klass);
    }
    else if (BUILTIN_TYPE(klass) == T_CLASS && FL_TEST(klass, FL_SINGLETON))
    {
        result = figure_singleton_name(klass);
    }
    else if (BUILTIN_TYPE(klass) == T_CLASS)
    {
        result = rb_class_name(klass);
    }
    else
    {
        /* Should never happen. */
        result = rb_str_new2("[unknown]");
    }

    return result;
}

static const char *get_class_name(VALUE klass) {
    VALUE name;

    if (klass == rb_cNilClass) {
        return "nil";
    }
    else if (klass == rb_cFalseClass || klass == rb_cTrueClass) {
        return "Boolean";
    }

    name = rb_rescue(klass_name, klass, NULL, Qnil);

    if (name == Qnil) {
        return "[error]";
    }
    else {
        return StringValuePtr(name);
    }
}

static void handle_line_event(VALUE tracepoint, trace_t *trace) {
    rb_trace_arg_t *trace_arg;

    trace_arg = rb_tracearg_from_tracepoint(tracepoint);

    trace->current_file_name = rb_tracearg_path(trace_arg);
    trace->current_line_number = FIX2INT(rb_tracearg_lineno(trace_arg));

    if (trace->frames_count == 0) {
        trace->frames_count += 1;
        trace->frames[trace->frames_count - 1].calls = 0;
    }

    // Sucks to do this on EVERY LINE
    trace->frames[trace->frames_count - 1].file_name = trace->current_file_name;
    if (rb_str_cmp(trace->current_file_name, trace->frames[trace->frames_count - 1].file_name) == 0) {
        trace->frames[trace->frames_count - 1].line_number = trace->current_line_number;
    }
}

/*
 * This function takes file_path and returns the first position that isn't
 * included in trace->project_root.
 *
 * It effectively returns the path to file_path, relative to the project root.
 *
 * Returns NULL when file_path is simply not relative to project_root or when
 * project_root is nil.
 */
static const char *relative_to_project_root(trace_t *trace, const char *file_path) {
    if (trace->project_root == Qnil) return NULL;
    const char *project_root = StringValuePtr(trace->project_root);

    const char *c1 = project_root;
    const char *c2 = file_path;
    while (*c1 && *c2) {
        if (*c1 != *c2) return NULL;
        c1++;
        c2++;
    }
    if (*c2 == 0 || *c2 != '/') return NULL;

    return c2 + 1;
}

/*
 * Check if the trace_arg's "path" attribute should be ignored for this trace.
 */
static int is_blocked(trace_t *trace, rb_trace_arg_t *trace_arg) {
    VALUE current_file_name, blocked_path;
    long i, len;
    const char *file, *blocked;

    if (trace->path_blocklist == Qnil) return 0;

    current_file_name = rb_tracearg_path(trace_arg);
    if (current_file_name == Qnil) return 1;

    file = StringValuePtr(current_file_name);
    len = RARRAY_LEN(trace->path_blocklist);

    for (i = 0; i < len; ++i) {
        blocked_path = RARRAY_AREF(trace->path_blocklist, i);
        blocked = StringValuePtr(blocked_path);

        if (strstr(file, blocked) != NULL) return 1;
    }

    return 0;
}

static int is_in_project_root(trace_t *trace, rb_trace_arg_t *trace_arg) {
    if (trace->project_root == Qnil) return 1;

    VALUE current_file_name_rstr = rb_tracearg_path(trace_arg);
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

static VALUE get_binding(VALUE tracepoint) {
    rb_trace_arg_t *trace_arg = rb_tracearg_from_tracepoint(tracepoint);
    return rb_tracearg_binding(trace_arg);
}

static void write_local_variables(
    trace_t *trace,
    VALUE tracepoint,
    const char *method_key
) {
    long i, len;
    const char *local_name, *local_type;
    VALUE binding, local_variables, variable_name, variable, variable_klass;
    char local_var_key[IDENTIFIER_MAX_SIZE];

    binding = rb_rescue(get_binding, tracepoint, NULL, Qnil);
    if (binding == Qnil) return;

    /*
     * local_variables = binding.local_variables
     * len = local_variables.size
     */
    local_variables = rb_funcall(binding, id_local_variables, 0);
    len = RARRAY_LEN(local_variables);

    for (i = 0; i < len; ++i) {
        /*
         * variable_name = local_variables[i]
         * variable = binding.local_variable_get(variable_name)
         * variable_klass = variable.class
         * local_name = variable_name.to_s
         * local_type = variable_klass.name
         */
        variable_name = RARRAY_AREF(local_variables, i);
        variable = rb_funcall(binding, id_local_variable_get, 1, variable_name);
        variable_klass = rb_obj_class(variable);
        local_name = rb_id2name(SYM2ID(variable_name));
        local_type = get_class_name(variable_klass);

        snprintf(
            local_var_key,
            sizeof(local_var_key),
            "%s%%%s",
            method_key, local_name
        );
        // TODO: probably don't need this whole block
        // filedict_insert_unique(&trace->locals, local_var_key, local_type);
    }
}

static void handle_call_event(VALUE tracepoint, trace_t *trace) {
    rb_trace_arg_t *trace_arg;
    trace_arg = rb_tracearg_from_tracepoint(tracepoint);

    VALUE callee = rb_tracearg_callee_id(trace_arg);
    VALUE klass = rb_tracearg_defined_class(trace_arg);

    // Only count calls to methods within the user's project
    if (is_in_project_root(trace, trace_arg)) {
        // We count for every frame in the stack.
        // If we have
        //   A: in project
        //   B: out of project
        //   C: in project
        // then this helps us achieve the following behavior:
        //   A(1) -> B(2) -> C(3) -> B(4)
        //   A(1): not leaf <-- without this counting logic, this would be counted as leaf
        //   B(2): not counted
        //   C(3): leaf
        //   B(4): not counted
        for (int i = 0; i < trace->frames_count; ++i) {
            trace->frames[i].calls += 1;
        }
    }

    if (trace->frames_count >= FRAMES_MAX) {
        printf("EXPOSURE OUT OF FRAMES!! probably won't function correctly\n");
        return;
    }

    trace->frames_count += 1;
    trace->frames[trace->frames_count - 1].calls = 0;

    // Break out the method name in Class#instance_method / Class.singleton_method format.
    const char *method_name = (callee != Qnil) ? rb_id2name(SYM2ID(callee)) : "<none>";
    const char *class_name = get_class_name(klass);
    int is_singleton = BUILTIN_TYPE(klass) == T_CLASS && FL_TEST(klass, FL_SINGLETON);
    char method_sep = is_singleton ? '.' : '#';

    snprintf(
        trace->frames[trace->frames_count - 1].method_key, METHOD_KEY_LEN,
        "%s%c%s",
        class_name, method_sep, method_name
    );
}

static void handle_return_event(VALUE tracepoint, trace_t *trace) {
    trace_frame_t *frame = &trace->frames[trace->frames_count - 1];
    int is_leaf = frame->calls == 0;

    if (is_leaf) {
        printf("LEAF CALL %s\n", frame->method_key);
    }

    if (trace->frames_count > 0) {
        trace->frames_count -= 1;
    }
}

/*
 * This is heavily "inspired" by ruby-prof
 * https://github.com/ruby-prof/ruby-prof/blob/master/ext/ruby_prof/rp_profile.c
 */
static void event_hook(VALUE tracepoint, void *data) {
    trace_t *trace = (trace_t *)data;
    rb_trace_arg_t *trace_arg;
    rb_event_flag_t event;

    trace_arg = rb_tracearg_from_tracepoint(tracepoint);
    event     = rb_tracearg_event_flag(trace_arg);

    if (event == RUBY_EVENT_LINE) handle_line_event(tracepoint, trace);
    if (event == RUBY_EVENT_CALL || event == RUBY_EVENT_C_CALL) handle_call_event(tracepoint, trace);
    if (event == RUBY_EVENT_RETURN || event == RUBY_EVENT_C_RETURN) handle_return_event(tracepoint, trace);
    else {
        printf("BUG: unhandled tracepoint event in exposure??");
    }
}


/*
 * =====================
 * Exported Ruby methods
 * =====================
 */

static VALUE trace_initialize(
    VALUE self,
    VALUE project_root,
    VALUE path_blocklist
) {
    trace_t *trace = RTYPEDDATA_DATA(self);

    trace->path_blocklist = path_blocklist;
    trace->project_root = project_root;

    return self;
}

static VALUE trace_tracepoint(VALUE self) {
    trace_t *trace = RTYPEDDATA_DATA(self);

    if (trace->tracepoint == Qnil) {
        trace->tracepoint = rb_tracepoint_new(
            Qnil,
            RUBY_EVENT_CALL | RUBY_EVENT_C_CALL | 
            RUBY_EVENT_RETURN | RUBY_EVENT_C_RETURN |
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
    PAGE_SIZE = (size_t)getpagesize();

    cTrace = rb_define_class_under(mExposure, "Trace", rb_cObject);
    rb_define_alloc_func(cTrace, trace_allocate);

    rb_define_method(cTrace, "initialize", trace_initialize, 2);
    rb_define_method(cTrace, "tracepoint", trace_tracepoint, 0);

    id_local_variables = rb_intern("local_variables");
    id_local_variable_get = rb_intern("local_variable_get");
}
