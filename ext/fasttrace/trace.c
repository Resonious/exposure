#include "trace.h"
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include "../../filedict/filedict.h"

ID id_local_variables;
ID id_local_variable_get;
VALUE cTrace;

/*
 * ==============================
 * Data structures for IPC
 * ==============================
 *
 * To send traces to the viewer app, we will use 2 files:
 * 1. The "entries" file - this is a big in-place array of fixed-sized offsets
 *    into ...
 * 2. The "data" file. This is a "heap" of variable-sized data, pointed to by
 *    the entries file. This contains class names, method names, file locations.
 */

static unsigned int ruby_event_to_entry_type(rb_event_flag_t event) {
    switch (event) {
    case RUBY_EVENT_CALL:
        return ENTRY_TYPE_CALL;
    case RUBY_EVENT_RETURN:
        return ENTRY_TYPE_RETURN;
    case RUBY_EVENT_B_CALL:
        return ENTRY_TYPE_CALL;
    case RUBY_EVENT_B_RETURN:
        return ENTRY_TYPE_RETURN;
    case RUBY_EVENT_C_CALL:
        return ENTRY_TYPE_CALL;
    case RUBY_EVENT_C_RETURN:
        return ENTRY_TYPE_RETURN;
    default:
        return 0;
    }
}

static size_t PAGE_SIZE;

static void trace_file_map_memory(trace_file_t *file) {
    assert(file);
    if (file->file == NULL) {
        rb_raise(rb_eRuntimeError, "File not open: %s", strerror(errno));
    }

    ftruncate(fileno(file->file), file->len);
    file->data = mmap(
        NULL,
        file->len - file->offset,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fileno(file->file),
        file->offset
    );

    if (file->data == MAP_FAILED) {
        rb_raise(rb_eRuntimeError, "Failed to mmap: %s", strerror(errno));
    }
}

static void trace_file_unmap_memory(trace_file_t *file) {
    assert(file);
    assert(file->file);
    assert(file->data);

    int result = munmap(file->data, file->len - file->offset);

    if (result == -1) {
        rb_raise(rb_eRuntimeError, "Failed to munmap: %s", strerror(errno));
    }

    file->data = NULL;
}

static void trace_file_resize(trace_file_t *file, size_t new_size) {
    trace_file_unmap_memory(file);
    file->offset = file->i;
    file->len = new_size;
    trace_file_map_memory(file);
}

/*
 * This unmaps the memory and truncates the file so that there's no extra zeros
 * at the end.
 */
static void trace_file_finalize(trace_file_t *file) {
    if (file->data) {
        trace_file_unmap_memory(file);
    }
    ftruncate(fileno(file->file), file->i);
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
    rb_gc_mark(trace->project_root);
    rb_gc_mark(trace->current_file_name);
}

static void trace_free(void *data) {
    trace_t *trace = (trace_t*)data;

    if (trace->entries.data) {
        trace_file_finalize(&trace->entries);
    }
    if (trace->entries.file) {
        fclose(trace->entries.file);
    }
    if (trace->strings_table) {
        st_free_table(trace->strings_table);
    }
    filedict_deinit(&trace->returns);
    filedict_deinit(&trace->locals);

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

    trace->entries.file = NULL;
    trace->entries.data = NULL;
    trace->entries.i = 0;
    trace->entries.offset = 0;
    trace->entries.len = 0;

    filedict_init(&trace->returns);
    filedict_init(&trace->locals);

    trace->strings_table = NULL;

    trace->project_root = Qnil;

    trace->running = 0;

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
    VALUE attached_str;
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
    VALUE name = rb_rescue(klass_name, klass, NULL, Qnil);

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

static int should_record(trace_t *trace, rb_trace_arg_t *trace_arg) {
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
    const char *class_name, char method_sep, const char *method_name
) {
    long i, len;
    const char *local_name, *local_type;
    VALUE binding, local_variables, variable_name, variable, variable_klass;
    char local_var_key[FILEDICT_KEY_SIZE];

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
            "%s%c%s%%%s",
            class_name, method_sep, method_name, local_name
        );
        filedict_insert_unique(&trace->locals, local_var_key, local_type);
    }
}

static void handle_call_or_return_event(VALUE tracepoint, trace_t *trace) {
    rb_trace_arg_t *trace_arg;
    rb_event_flag_t event;

    VALUE callee, klass, return_value, return_klass;
    int is_singleton = 0;
    const char *class_name;
    const char *method_name_cstr;
    const char *return_type;
    char method_sep;
    char method_key[FILEDICT_KEY_SIZE];

    trace_arg = rb_tracearg_from_tracepoint(tracepoint);

    if (!should_record(trace, trace_arg)) return;

    event = rb_tracearg_event_flag(trace_arg);

    callee         = rb_tracearg_callee_id(trace_arg);
    klass          = rb_tracearg_defined_class(trace_arg);

    /* Class#new is a nuisance because it generates a lot of different return types. */
    if (klass == rb_cClass) return;

    class_name     = get_class_name(klass);
    is_singleton   = BUILTIN_TYPE(klass) == T_CLASS && FL_TEST(klass, FL_SINGLETON);

    method_name_cstr = (callee != Qnil ? rb_id2name(SYM2ID(callee)) : "<none>");

    if (is_singleton) method_sep = '.';
    else              method_sep = '#';

    /* Put the actual trace event into the entries file */
    trace_entry_t *entry = (trace_entry_t *)
        (trace->entries.data + (trace->entries.i - trace->entries.offset));

    entry->type = ruby_event_to_entry_type(event);

    /* From here on we assume that this is a RETURN event */
    return_value = rb_tracearg_return_value(trace_arg);
    return_klass = rb_obj_class(return_value);
    return_type  = get_class_name(return_klass);

    snprintf(
        method_key,
        sizeof(method_key),
        "%s%c%s",
        class_name, method_sep, method_name_cstr
    );
    filedict_insert_unique(&trace->returns, method_key, return_type);
    write_local_variables(trace, tracepoint, class_name, method_sep, method_name_cstr);
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

    if (event == RUBY_EVENT_LINE) handle_line_event(tracepoint, (trace_t *)data);
    else handle_call_or_return_event(tracepoint, (trace_t *)data);
}


/*
 * =====================
 * Exported Ruby methods
 * =====================
 */

static VALUE trace_initialize(VALUE self, VALUE trace_entries_filename, VALUE project_root) {
    trace_t *trace = RTYPEDDATA_DATA(self);
    const char *trace_entries_filename_cstr = StringValuePtr(trace_entries_filename);

    VALUE trace_returns_path = rb_str_new(trace_entries_filename_cstr, RSTRING_LEN(trace_entries_filename));
    rb_str_append(trace_returns_path, rb_str_new_literal(".returns"));
    const char *trace_returns_path_cstr = StringValuePtr(trace_returns_path);

    VALUE trace_locals_path = rb_str_new(trace_entries_filename_cstr, RSTRING_LEN(trace_entries_filename));
    rb_str_append(trace_locals_path, rb_str_new_literal(".locals"));
    const char *trace_locals_path_cstr = StringValuePtr(trace_locals_path);

    trace->entries.file = fopen(trace_entries_filename_cstr, "wb+");

    /* We expect these traces to be large */
    filedict_open_f(&trace->returns, trace_returns_path_cstr, O_CREAT | O_RDWR, 4096 * 5);
    filedict_open_f(&trace->locals, trace_locals_path_cstr, O_CREAT | O_RDWR, 4096 * 5);

    trace->project_root = project_root;

    trace->entries.len = PAGE_SIZE;
    trace_file_map_memory(&trace->entries);

    trace->strings_table = st_init_strtable_with_size(4096);

    if (sizeof(trace_entry_t) > 64) {
        rb_raise(rb_eRuntimeError, "Trace entry struct not within 64 bytes? %ld", sizeof(trace_entry_t));
    }

    return self;
}

static VALUE trace_tracepoint(VALUE self) {
    trace_t *trace = RTYPEDDATA_DATA(self);

    if (trace->tracepoint == Qnil) {
        trace->tracepoint = rb_tracepoint_new(
            Qnil,
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

    cTrace = rb_define_class_under(mFasttrace, "Trace", rb_cObject);
    rb_define_alloc_func(cTrace, trace_allocate);

    rb_define_method(cTrace, "initialize", trace_initialize, 2);
    rb_define_method(cTrace, "tracepoint", trace_tracepoint, 0);

    id_local_variables = rb_intern("local_variables");
    id_local_variable_get = rb_intern("local_variable_get");
}
