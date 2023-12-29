#include "trace.h"
#include "ruby/debug.h"
#include "ruby/internal/event.h"
#include "ruby/internal/globals.h"
#include "ruby/internal/intern/cont.h"
#include "ruby/internal/intern/string.h"
#include "ruby/internal/special_consts.h"
#include "ruby/internal/value_type.h"
#include "ruby/ruby.h"
#include "ruby/st.h"
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

static int mark_traced_fiber(st_data_t key, st_data_t value, st_data_t _args) {
    VALUE fiber = (VALUE)key;
    trace_stack_t *stack = (trace_stack_t *)value;
    rb_gc_mark(fiber);

    rb_gc_mark(stack->current_file_name);
    rb_gc_mark(stack->callee);
    rb_gc_mark(stack->klass);
    for (int i = 0; i < stack->frames_count; ++i) {
        rb_gc_mark(stack->frames[i].file_name);
    }

    return ST_CONTINUE;
}

static int free_traced_fiber(st_data_t _key, st_data_t value, st_data_t _args) {
    trace_stack_t *stack = (trace_stack_t *)value;
    if (stack->name != NULL) xfree(stack->name);
    xfree(stack);
    return ST_CONTINUE;
}

static void trace_mark(void *data) {
    trace_t *trace = (trace_t*)data;
    rb_gc_mark(trace->tracepoint);
    rb_gc_mark(trace->project_root);
    st_foreach(trace->fibers_table, mark_traced_fiber, 0);
}

static void trace_free(void *data) {
    trace_t *trace = (trace_t*)data;

    if (trace->fibers_table) {
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
    trace->project_root = Qnil;

    return result;
}


/*
 * =============
 * Trace methods
 * =============
 */

static void stack_init(trace_stack_t *stack, VALUE fiber) {
    stack->frames_count = 0;
    stack->callee = Qnil;
    stack->klass = Qnil;
    stack->new_call = 0;
    stack->name = NULL;

    VALUE fiber_name = rb_sprintf("Fiber %i", FIX2INT(rb_obj_id(fiber)));
    unsigned long long fiber_name_len = RSTRING_LEN(fiber_name);

    stack->name = xmalloc(fiber_name_len + 1);
    memcpy(stack->name, RSTRING_PTR(fiber_name), fiber_name_len);
    stack->name[fiber_name_len] = '\0';
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

static trace_stack_t *stack_for_fiber(trace_t *trace, VALUE fiber) {
    trace_stack_t *stack;
    st_data_t data;

    if (!trace->fibers_table) {
        trace->fibers_table = st_init_table_with_size(&fibers_table_type, 16);
    }

    if (st_lookup(trace->fibers_table, fiber, &data)) {
        stack = (trace_stack_t *)data;
    } else {
        stack = xmalloc(sizeof(trace_stack_t));
        stack_init(stack, fiber);
        st_insert(trace->fibers_table, (st_data_t)fiber, (st_data_t)stack);
    }

    return stack;
}

static int is_in_project_root(trace_t *trace, rb_trace_arg_t *trace_arg) {
    if (trace->project_root == Qnil) return 1;

    VALUE current_file_name_rstr = rb_tracearg_path(trace_arg);
    if (current_file_name_rstr == Qnil) return 0;

    const char *current_file_name = StringValuePtr(current_file_name_rstr);
    const char *project_root = StringValuePtr(trace->project_root);

    const char *c1 = project_root;
    const char *c2 = current_file_name;

    // This is probably something like "<internal:...>" which is never local
    if (c2[0] == '<') return 0;

    // Likely "(eval)" which is assumed not local (can't really tell in this case)
    if (c2[0] == '(') return 0;

    // Relative paths are assumed to be local
    rb_event_flag_t event = rb_tracearg_event_flag(trace_arg);
    if (event == RUBY_EVENT_LINE && c2[0] != '/') return 1;

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

    if (klass == rb_cNilClass || klass == Qnil) {
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

static void record_new_call(rb_trace_arg_t *trace_arg, trace_t *trace) {
    VALUE fiber = rb_fiber_current();
    trace_stack_t *stack = stack_for_fiber(trace, fiber);

    VALUE callee = stack->callee;
    VALUE klass = stack->klass;
    stack->callee = Qnil;
    stack->klass = Qnil;

    // TODO: despite these checks, I sometimes still see BasicObject show up.
    int check_root = 1;
    if (
        klass == rb_cModule || klass == rb_cClass ||
        klass == rb_cBasicObject
    ) {
        check_root = 0;
    }

    int is_in_root = check_root && is_in_project_root(trace, trace_arg);

    // Only count calls to methods within the user's project
    if (is_in_root) {
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
        for (int i = 0; i < stack->frames_count; ++i) {
            stack->frames[i].calls += 1;
        }
    }

    if (stack->frames_count >= FRAMES_MAX) {
        printf("EXPOSURE OUT OF FRAMES!! probably won't function correctly\n");
        return;
    }

    stack->frames_count += 1;
    stack->frames[stack->frames_count - 1].calls = 0;
    stack->frames[stack->frames_count - 1].is_in_root = is_in_root;
    stack->frames[stack->frames_count - 1].file_name = stack->current_file_name;
    stack->frames[stack->frames_count - 1].line_number = stack->current_line_number;

    // Break out the method name in Class#instance_method / Class.singleton_method format.
    const char *method_name = (callee != Qnil) ? rb_id2name(SYM2ID(callee)) : "<none>";
    const char *class_name = get_class_name(klass);
    int is_singleton = klass == Qnil || (BUILTIN_TYPE(klass) == T_CLASS && FL_TEST(klass, FL_SINGLETON));
    char method_sep = is_singleton ? '.' : '#';

    snprintf(
        stack->frames[stack->frames_count - 1].method_key, METHOD_KEY_LEN,
        "%s%c%s",
        class_name, method_sep, method_name
    );

    // rb_event_flag_t event = rb_tracearg_event_flag(trace_arg);
    // VALUE current_file_name_rstr = rb_tracearg_path(trace_arg);
    // printf("Creating frame %s %s (in root %i) %s\n", get_event_name(event), stack->frames[stack->frames_count - 1].method_key, is_in_root, rb_string_value_ptr(&current_file_name_rstr));
}

static void handle_line_event(VALUE tracepoint, trace_t *trace) {
    VALUE fiber = rb_fiber_current();
    trace_stack_t *stack = stack_for_fiber(trace, fiber);

    rb_trace_arg_t *trace_arg;

    trace_arg = rb_tracearg_from_tracepoint(tracepoint);

    if (stack->new_call) {
        stack->new_call = 0;
        record_new_call(trace_arg, trace);
    }

    stack->current_file_name = rb_tracearg_path(trace_arg);
    stack->current_line_number = FIX2INT(rb_tracearg_lineno(trace_arg));

    if (stack->frames_count == 0) {
        stack->frames_count += 1;
        stack->frames[stack->frames_count - 1].calls = 0;
        stack->frames[stack->frames_count - 1].is_in_root = is_in_project_root(trace, trace_arg);
        stack->frames[stack->frames_count - 1].file_name = Qnil;
        stack->frames[stack->frames_count - 1].line_number = 0;
    }

    // Sucks to do this on EVERY LINE
    stack->frames[stack->frames_count - 1].file_name = stack->current_file_name;
    if (rb_str_cmp(stack->current_file_name, stack->frames[stack->frames_count - 1].file_name) == 0) {
        stack->frames[stack->frames_count - 1].line_number = stack->current_line_number;
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
    VALUE fiber = rb_fiber_current();
    trace_stack_t *stack = stack_for_fiber(trace, fiber);

    rb_trace_arg_t *trace_arg;
    trace_arg = rb_tracearg_from_tracepoint(tracepoint);

    stack->callee = rb_tracearg_callee_id(trace_arg);
    stack->klass = rb_tracearg_defined_class(trace_arg);

    rb_event_flag_t event = rb_tracearg_event_flag(trace_arg);
    if (event == RUBY_EVENT_C_CALL) {
        record_new_call(trace_arg, trace);
    } else {
        stack->new_call = 1;
    }
}

static void handle_return_event(VALUE tracepoint, trace_t *trace) {
    VALUE fiber = rb_fiber_current();
    trace_stack_t *stack = stack_for_fiber(trace, fiber);

    if (stack->frames_count <= 0) {
        return;
    }

    rb_trace_arg_t *trace_arg = rb_tracearg_from_tracepoint(tracepoint);
    rb_event_flag_t event     = rb_tracearg_event_flag(trace_arg);

    trace_frame_t *frame = &stack->frames[stack->frames_count - 1];
    int is_leaf = event != RUBY_EVENT_C_RETURN && frame->calls == 0;

    // TODO: would love to grab local variable values right here too and report them.

    // DEBUG PRINT (REPLACE WITH REAL SEND)
    if (frame->is_in_root && is_leaf) {
        printf("LEAF CALL (%s) (%s:%i) (%s) %s\n", get_event_name(event), RSTRING_PTR(frame->file_name), frame->line_number, stack->name ? stack->name : "??", frame->method_key);
    }
    // END DEBUG PRINT

    stack->frames_count -= 1;
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
    else if (event == RUBY_EVENT_CALL || event == RUBY_EVENT_C_CALL) handle_call_event(tracepoint, trace);
    else if (event == RUBY_EVENT_RETURN || event == RUBY_EVENT_C_RETURN) handle_return_event(tracepoint, trace);
    else {
        printf("BUG: unhandled tracepoint event in exposure??\n");
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
    trace->fibers_table = NULL;

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

    fibers_table_type.compare = rb_value_compare;
    fibers_table_type.hash = rb_value_hash;

    cTrace = rb_define_class_under(mExposure, "Trace", rb_cObject);
    rb_define_alloc_func(cTrace, trace_allocate);

    rb_define_method(cTrace, "initialize", trace_initialize, 2);
    rb_define_method(cTrace, "tracepoint", trace_tracepoint, 0);

    id_local_variables = rb_intern("local_variables");
    id_local_variable_get = rb_intern("local_variable_get");
}
