#include "trace.h"
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

VALUE cTrace;
const unsigned int kSingleton = kClassSingleton | kModuleSingleton |
                                kObjectSingleton | kOtherSingleton;

/*
 * ==============================
 * Data structures for IPC
 * ==============================
 *
 * To send traces to the viewer app, we will use 2 files:
 * 1. The "header" file - this is a big in-place array of fixed-sized offsets
 *    into ...
 * 2. The "data" file. This is a "heap" of variable-sized data, pointed to by
 *    the header file. This contains class names, method names, file locations.
 */

const unsigned int HEADER_TYPE_CALL = 1;
const unsigned int HEADER_TYPE_RETURN = 2;

/*
 * NOTE: this struct needs to have a byte size that is a power of 2.
 */
typedef struct trace_header_t {
    unsigned long long type : 32;

    unsigned long long method_name_start : 64;
    unsigned long long method_name_len : 32;

    unsigned long long caller_file_start : 64;
    unsigned long long caller_file_len : 32;
    unsigned long long caller_line_number : 32;

    unsigned long long callee_file_start : 64;
    unsigned long long callee_file_len : 32;
    unsigned long long callee_line_number : 32;

    /* TODO: 16 more bytes... */
} __attribute__ ((__packed__)) trace_header_t;

static unsigned int ruby_event_to_header_type(rb_event_flag_t event) {
    switch (event) {
    case RUBY_EVENT_CALL:
        return HEADER_TYPE_CALL;
    case RUBY_EVENT_RETURN:
        return HEADER_TYPE_RETURN;
    case RUBY_EVENT_B_CALL:
        return HEADER_TYPE_CALL;
    case RUBY_EVENT_B_RETURN:
        return HEADER_TYPE_RETURN;
    case RUBY_EVENT_C_CALL:
        return HEADER_TYPE_CALL;
    case RUBY_EVENT_C_RETURN:
        return HEADER_TYPE_RETURN;
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
}

static void trace_free(void *data) {
    trace_t *trace = (trace_t*)data;

    if (trace->header.data) {
        trace_file_finalize(&trace->header);
    }
    if (trace->strings.data) {
        trace_file_finalize(&trace->strings);
    }
    if (trace->header.file) {
        fclose(trace->header.file);
    }
    if (trace->strings.file) {
        fclose(trace->strings.file);
    }
    if (trace->strings_table) {
        st_free_table(trace->strings_table);
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

    trace->header.file = NULL;
    trace->header.data = NULL;
    trace->header.i = 0;
    trace->header.offset = 0;
    trace->header.len = 0;

    trace->strings.file = NULL;
    trace->strings.data = NULL;
    trace->strings.i = 0;
    trace->strings.offset = 0;
    trace->strings.len = 0;

    trace->strings_table = NULL;

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

static void handle_call_or_return_event(VALUE tracepoint, trace_t *trace) {
    rb_trace_arg_t *trace_arg;
    rb_event_flag_t event;
    VALUE fiber;

    const char *event_name;
    VALUE source_file;
    int source_line;
    VALUE callee, klass;
    unsigned int class_flags;
    const char *class_name;
    const char *method_name_cstr;
    const char *source_file_cstr;
    char method_sep;

    char *buffer;
    size_t method_name_start;
    int method_name_len;

    fiber = rb_fiber_current();

    trace_arg = rb_tracearg_from_tracepoint(tracepoint);
    event     = rb_tracearg_event_flag(trace_arg);

    event_name     = get_event_name(event);
    source_file    = rb_tracearg_path(trace_arg);
    source_line    = FIX2INT(rb_tracearg_lineno(trace_arg));
    callee         = rb_tracearg_callee_id(trace_arg);
    klass          = rb_tracearg_defined_class(trace_arg);
    class_name     = get_class_name(klass, &class_flags);

    method_name_cstr = (callee != Qnil ? rb_id2name(SYM2ID(callee)) : "<none>");
    source_file_cstr = (source_file != Qnil ? StringValuePtr(source_file) : "<none>");

    if (class_flags & kSingleton) method_sep = '.';
    else                          method_sep = '#';

    /* For now, stupidly put all strings into the strings file */
retry_method_name:
    buffer = trace->strings.data + (trace->strings.i - trace->strings.offset);

    method_name_len = snprintf(
        buffer,
        trace->strings.len - trace->strings.i,
        "%s%c%s",
        class_name, method_sep, method_name_cstr
    );

    if (method_name_len <= 0) {
        printf(
            "failed to sprintf method name ?? %s%c%s %s\n",
            class_name, method_sep, method_name_cstr, strerror(errno)
        );
        printf("%i\n", trace->strings.len - trace->strings.i);
        assert(method_name_len > 0);
    }

    /* Need to resize and try again if the full formatted string is too long */
    if ((trace->strings.i + method_name_len) >= trace->strings.len) {
        trace->strings.i = trace->strings.len;
        trace_file_resize(&trace->strings, trace->strings.len * 2);
        goto retry_method_name;
    }
    method_name_start = trace->strings.i;
    trace->strings.i += method_name_len;

    /* Put the actual trace event into the header file
     * (man why do I call it header...) */
    trace_header_t *entry = (trace_header_t *)
        (trace->header.data + (trace->header.i - trace->header.offset));

    entry->type = ruby_event_to_header_type(event);
    entry->method_name_start = method_name_start;
    entry->method_name_len = method_name_len;
    entry->caller_file_start = 0;
    entry->caller_file_len = 0;
    entry->caller_line_number = trace->current_line_number;
    entry->callee_file_start = 0;
    entry->callee_file_len = 0;
    entry->callee_line_number = source_line;

    trace->header.i += 64;

    /* Re-adjust header mapping once we run out of space */
    if (trace->header.i >= trace->header.len) {
        trace_file_resize(&trace->header, trace->header.len * 2);
    }

    return;
    fprintf(
        trace->header.file, "%2lu:%2f\t%-8s\t%s%c%s\t%s:%2d\n",
        FIX2ULONG(fiber), measure_wall_time(),
        event_name, class_name, method_sep, method_name_cstr, source_file_cstr, source_line
    );
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

static VALUE trace_initialize(VALUE self, VALUE trace_header_name) {
    trace_t *trace = RTYPEDDATA_DATA(self);
    const char *trace_header_name_cstr = StringValuePtr(trace_header_name);

    VALUE trace_data_name = rb_str_new(trace_header_name_cstr, RSTRING_LEN(trace_header_name));
    rb_str_append(trace_data_name, rb_str_new_literal(".strings"));
    const char *trace_data_name_cstr = StringValuePtr(trace_data_name);

    trace->header.file = fopen(trace_header_name_cstr, "wb+");
    trace->strings.file = fopen(trace_data_name_cstr, "wb+");

    /* Just to make sure I know what I'm doing... */
    trace->strings.len = PAGE_SIZE;
    trace_file_map_memory(&trace->strings);

    trace->header.len = PAGE_SIZE;
    trace_file_map_memory(&trace->header);

    trace->strings_table = st_init_strtable_with_size(4096);

    if (sizeof(trace_header_t) > 64) {
        rb_raise(rb_eRuntimeError, "Trace header not within 64 bytes? %ld", sizeof(trace_header_t));
    }

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
    PAGE_SIZE = (size_t)getpagesize();

    cTrace = rb_define_class_under(mFasttrace, "Trace", rb_cObject);
    rb_define_alloc_func(cTrace, trace_allocate);

    rb_define_method(cTrace, "initialize", trace_initialize, 1);
    rb_define_method(cTrace, "tracepoint", trace_tracepoint, 0);
}
