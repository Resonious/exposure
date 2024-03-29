#include "trace.h"
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <string.h>
#include "../../filedict/filedict.h"

ID id_local_variables;
ID id_local_variable_get;
VALUE cTrace;

static size_t PAGE_SIZE;
#define IDENTIFIER_MAX_SIZE (FILEDICT_BUCKET_ENTRY_BYTES * 2)


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
}

static void trace_free(void *data) {
    trace_t *trace = (trace_t*)data;

    filedict_deinit(&trace->returns);
    filedict_deinit(&trace->locals);
    filedict_deinit(&trace->blocks);

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

    filedict_init(&trace->returns);
    filedict_init(&trace->locals);
    filedict_init(&trace->blocks);

    trace->project_root = Qnil;

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
        filedict_insert_unique(&trace->locals, local_var_key, local_type);
    }
}

/*
 * On a b_return event, we record the return type of the block, as well as the
 * types of all its local variables and parameters.
 * Additionally, we record the receiver.
 */
static void handle_b_return_event(VALUE tracepoint, trace_t *trace) {
    rb_trace_arg_t *trace_arg;
    VALUE file_name_val, return_value, return_klass, receiver, receiver_class;
    const char *file_name, *relative_file_name, *return_type, *receiver_type;
    int line_number;
    char block_key[IDENTIFIER_MAX_SIZE];

    trace_arg = rb_tracearg_from_tracepoint(tracepoint);

    file_name_val = rb_tracearg_path(trace_arg);
    file_name     = StringValuePtr(file_name_val);

    relative_file_name = relative_to_project_root(trace, file_name);
    /* There is no value in analyzing blocks outside of the current project */
    if (relative_file_name == NULL) return;
    if (is_blocked(trace, trace_arg)) return;

    line_number  = FIX2INT(rb_tracearg_lineno(trace_arg));
    return_value = rb_tracearg_return_value(trace_arg);
    return_klass = rb_obj_class(return_value);
    return_type  = get_class_name(return_klass);

    /*
     * We identify blocks by the line number of the block's end token.
     * This is potentially ambiguous, as you could in theory have multiple
     * blocks on the same line. Additionally, editing the code is likely to
     * move blocks around. Not much we can do about these issues!
     */
    snprintf(
        block_key,
        sizeof(block_key),
        "%s:%i",
        relative_file_name, line_number
    );

    write_local_variables(trace, tracepoint, block_key);
    filedict_insert_unique(&trace->returns, block_key, return_type);

    if (trace->track_block_receivers) {
        receiver = rb_tracearg_self(trace_arg);
        if (receiver == Qnil) return;
        receiver_class = rb_obj_class(receiver);
        receiver_type = get_class_name(receiver_class);
        filedict_insert_unique(&trace->blocks, block_key, receiver_type);
    }
}

static void handle_call_or_return_event(VALUE tracepoint, trace_t *trace) {
    rb_trace_arg_t *trace_arg;

    VALUE callee, klass, return_value, return_klass;
    int is_singleton = 0;
    const char *class_name;
    const char *method_name_cstr;
    const char *return_type;
    char method_sep;
    char method_key[IDENTIFIER_MAX_SIZE];

    trace_arg = rb_tracearg_from_tracepoint(tracepoint);

    callee         = rb_tracearg_callee_id(trace_arg);
    klass          = rb_tracearg_defined_class(trace_arg);

    /* Class#new is a nuisance because it generates a lot of different return types. */
    /* TODO: Array methods also tend to get rather large. Maybe simply putting a cap
     * on how many return types a single method can hold is a better solution. */
    if (klass == rb_cClass) return;

    class_name     = get_class_name(klass);
    is_singleton   = BUILTIN_TYPE(klass) == T_CLASS && FL_TEST(klass, FL_SINGLETON);

    method_name_cstr = (callee != Qnil ? rb_id2name(SYM2ID(callee)) : "<none>");

    if (is_singleton) method_sep = '.';
    else              method_sep = '#';

    /* From here on we assume that this is a RETURN event */
    return_value = rb_tracearg_return_value(trace_arg);
    return_klass = rb_obj_class(return_value);
    return_type  = get_class_name(return_klass);

record_entry:
    snprintf(
        method_key,
        sizeof(method_key),
        "%s%c%s",
        class_name, method_sep, method_name_cstr
    );
    filedict_insert_unique(&trace->returns, method_key, return_type);

    /*
     * Analyzing locals has a pretty large performance penalty, so we try to only do
     * it for local files.
     */
    if (is_in_project_root(trace, trace_arg) && !is_blocked(trace, trace_arg)) {
        write_local_variables(trace, tracepoint, method_key);
    }

    /* For modules, we want data for both the module and the including class */
    if (!is_singleton && BUILTIN_TYPE(klass) == T_MODULE) {
        VALUE self = rb_tracearg_self(trace_arg);
        if (self == Qnil) return;

        klass = rb_obj_class(self);
        class_name = get_class_name(klass);
        goto record_entry;
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
    else if (event == RUBY_EVENT_B_RETURN) handle_b_return_event(tracepoint, trace);
    else handle_call_or_return_event(tracepoint, trace);
}


/*
 * =====================
 * Exported Ruby methods
 * =====================
 */

static VALUE trace_initialize(
    VALUE self,
    VALUE trace_entries_dir,
    VALUE project_root,
    VALUE path_blocklist,
    VALUE track_block_receivers
) {
    trace_t *trace = RTYPEDDATA_DATA(self);
    const char *trace_entries_filename_cstr = StringValuePtr(trace_entries_dir);

    VALUE trace_returns_path = rb_str_new(trace_entries_filename_cstr, RSTRING_LEN(trace_entries_dir));
    rb_str_append(trace_returns_path, rb_str_new_literal("/exposure.returns"));
    const char *trace_returns_path_cstr = StringValuePtr(trace_returns_path);

    VALUE trace_locals_path = rb_str_new(trace_entries_filename_cstr, RSTRING_LEN(trace_entries_dir));
    rb_str_append(trace_locals_path, rb_str_new_literal("/exposure.locals"));
    const char *trace_locals_path_cstr = StringValuePtr(trace_locals_path);

    VALUE trace_blocks_path = rb_str_new(trace_entries_filename_cstr, RSTRING_LEN(trace_entries_dir));
    rb_str_append(trace_blocks_path, rb_str_new_literal("/exposure.blocks"));
    const char *trace_blocks_path_cstr = StringValuePtr(trace_blocks_path);

    /* We expect these traces to be large */
    filedict_open_f(&trace->returns, trace_returns_path_cstr, O_CREAT | O_RDWR, 4096 * 5);
    filedict_open_f(&trace->locals, trace_locals_path_cstr, O_CREAT | O_RDWR, 4096 * 5);
    filedict_open_f(&trace->blocks, trace_blocks_path_cstr, O_CREAT | O_RDWR, 4096 * 5);

    trace->track_block_receivers = (track_block_receivers != Qfalse && track_block_receivers != Qnil);
    trace->path_blocklist = path_blocklist;
    trace->project_root = project_root;

    return self;
}

static VALUE trace_tracepoint(VALUE self) {
    trace_t *trace = RTYPEDDATA_DATA(self);

    if (trace->tracepoint == Qnil) {
        trace->tracepoint = rb_tracepoint_new(
            Qnil,
            RUBY_EVENT_RETURN | RUBY_EVENT_C_RETURN |
            RUBY_EVENT_B_RETURN |
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

    rb_define_method(cTrace, "initialize", trace_initialize, 4);
    rb_define_method(cTrace, "tracepoint", trace_tracepoint, 0);

    id_local_variables = rb_intern("local_variables");
    id_local_variable_get = rb_intern("local_variable_get");
}
