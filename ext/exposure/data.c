#include "data.h"
#include <stdint.h>

#define abd_assert(x) do {} while(0)

#define abd_write_field_header(buf, type, annotation) (buf)->bytes[(buf)->pos++] = (type) | ((annotation) ? ABDF_ANNOTATED : 0);

static void write_1_byte(AbdBuffer* buf, void* data) {
    memcpy(buf->bytes + buf->pos, data, 1);
    buf->pos += 1;
}

static void read_1_byte(AbdBuffer* buf, void* dest) {
    memcpy(dest, buf->bytes + buf->pos, 1);
    buf->pos += 1;
}

static void write_2_bytes(AbdBuffer* buf, void* data) {
    memcpy(buf->bytes + buf->pos, data, 2);
    buf->pos += 2;
}

static void read_2_bytes(AbdBuffer* buf, void* dest) {
    memcpy(dest, buf->bytes + buf->pos, 2);
    buf->pos += 2;
}

static void write_4_bytes(AbdBuffer* buf, void* data) {
    memcpy(buf->bytes + buf->pos, data, 4);
    buf->pos += 4;
}

static void read_4_bytes(AbdBuffer* buf, void* dest) {
    memcpy(dest, buf->bytes + buf->pos, 4);
    buf->pos += 4;
}

static void write_8_bytes(AbdBuffer* buf, void* data) {
    memcpy(buf->bytes + buf->pos, data, 8);
    buf->pos += 8;
}

static void read_8_bytes(AbdBuffer* buf, void* dest) {
    memcpy(dest, buf->bytes + buf->pos, 8);
    buf->pos += 8;
}

static void write_3_floats(AbdBuffer* buf, void* data) {
    memcpy(buf->bytes + buf->pos, data, 12);
    buf->pos += 12;
}

static void read_3_floats(AbdBuffer* buf, void* dest) {
    memcpy(dest, buf->bytes + buf->pos, 12);
    buf->pos += 12;
}

static void write_4_floats(AbdBuffer* buf, void* data) {
    memcpy(buf->bytes + buf->pos, data, 16);
    buf->pos += 16;
}

static void read_4_floats(AbdBuffer* buf, void* dest) {
    memcpy(dest, buf->bytes + buf->pos, 16);
    buf->pos += 16;
}

void abd_write_string(AbdBuffer* buf, void* str) {
    char* string = (char*)str;

    size_t str_size = strlen(string);
    size_t actual_size = str_size + 1;
    // TODO: haha maybe check for overflow..
    uint16_t actual_size_u16 = (uint16_t)actual_size;

    memcpy(buf->bytes + buf->pos, &actual_size_u16, sizeof(uint16_t));
    buf->pos += sizeof(uint16_t);
    memcpy(buf->bytes + buf->pos, string, actual_size);
    buf->pos += actual_size_u16;
}

void abd_read_string(AbdBuffer* buf, void* dest) {
    uint16_t length;
    memcpy(&length, buf->bytes + buf->pos, sizeof(uint16_t));
    buf->pos += sizeof(uint16_t);
    if (dest)
        memcpy(dest, buf->bytes + buf->pos, length);
    buf->pos += length;
}

static void inspect_float(AbdBuffer* buf, uint8_t type, FILE* f) {
    float fl;
    abd_data_read[type](buf, &fl);
    fprintf(f, "%f", fl);
}

static void inspect_integer_type(AbdBuffer* buf, uint8_t type, FILE* f) {
    int64_t i = 0;
    abd_data_read[type](buf, &i);
    fprintf(f, "%li", i);
}

static void inspect_unsigned_integer_type(AbdBuffer* buf, uint8_t type, FILE* f) {
    uint64_t i = 0;
    abd_data_read[type](buf, &i);
    fprintf(f, "%lu", i);
}

static void inspect_vec2(AbdBuffer* buf, uint8_t type, FILE* f) {
    float v[2];
    abd_data_read[type](buf, v);
    fprintf(f, "(%f, %f)", v[0], v[1]);
}

static void inspect_vec3(AbdBuffer* buf, uint8_t type, FILE* f) {
    float v[3];
    abd_data_read[type](buf, v);
    fprintf(f, "(%f, %f, %f)", v[0], v[1], v[2]);
}

static void inspect_vec4(AbdBuffer* buf, uint8_t type, FILE* f) {
    float v[4];
    abd_data_read[type](buf, v);
    fprintf(f, "(%f, %f, %f, %f)", v[0], v[1], v[2], v[3]);
}

static void inspect_color(AbdBuffer* buf, uint8_t type, FILE* f) {
    uint8_t c[4];
    abd_data_read[type](buf, c);
    fprintf(f, "#%02x%02x%02x%02x", c[0], c[1], c[2], c[3]);
}

static void inspect_bool(AbdBuffer* buf, uint8_t type, FILE* f) {
    bool b;
    abd_data_read[type](buf, &b);
    if (b)
        fprintf(f, "true");
    else
        fprintf(f, "false");
}

static void inspect_string(AbdBuffer* buf, uint8_t type, FILE* f) {
    char string[256];
    abd_read_string(buf, string);
    fprintf(f, "\"%s\"", string);
}

DataFunc abd_data_write[] = {
    write_4_bytes,   // ABDT_FLOAT
    write_8_bytes,   // ABDT_VEC2
    write_3_floats,  // ABDT_VEC3
    write_4_floats,  // ABDT_VEC4
    write_1_byte,    // ABDT_S8
    write_2_bytes,   // ABDT_S16
    write_4_bytes,   // ABDT_S32
    write_8_bytes,   // ABDT_S64
    write_1_byte,    // ABDT_U8
    write_2_bytes,   // ABDT_U16
    write_4_bytes,   // ABDT_U32
    write_8_bytes,   // ABDT_U64
    write_4_bytes,   // ABDT_COLOR
    write_1_byte,    // ABDT_BOOL
    abd_write_string // ABDT_STRING
};

DataFunc abd_data_read[] = {
    read_4_bytes,   // ABDT_FLOAT
    read_8_bytes,   // ABDT_VEC2
    read_3_floats,  // ABDT_VEC3
    read_4_floats,  // ABDT_VEC4
    read_1_byte,    // ABDT_S8
    read_2_bytes,   // ABDT_S16
    read_4_bytes,   // ABDT_S32
    read_8_bytes,   // ABDT_S64
    read_1_byte,    // ABDT_U8
    read_2_bytes,   // ABDT_U16
    read_4_bytes,   // ABDT_U32
    read_8_bytes,   // ABDT_U64
    read_4_bytes,   // ABDT_COLOR
    read_1_byte,    // ABDT_BOOL
    abd_read_string // ABDT_STRING
};

DataInspectFunc abd_data_inspect[] = {
    inspect_float,        // ABDT_FLOAT
    inspect_vec2,         // ABDT_VEC2
    inspect_vec3,         // ABDT_VEC3
    inspect_vec4,         // ABDT_VEC4
    inspect_integer_type, // ABDT_S8
    inspect_integer_type, // ABDT_S16
    inspect_integer_type, // ABDT_S32
    inspect_integer_type, // ABDT_S64
    inspect_unsigned_integer_type, // ABDT_U8
    inspect_unsigned_integer_type, // ABDT_U16
    inspect_unsigned_integer_type, // ABDT_U32
    inspect_unsigned_integer_type, // ABDT_U64
    inspect_color,        // ABDT_COLOR
    inspect_bool,         // ABDT_BOOL
    inspect_string        // ABDT_STRING
};

void abd_section(int rw, AbdBuffer* buf, char* section_label) {
    switch (rw) {
    case ABD_READ: {
        uint8_t read_type;
        abd_read_field(buf, &read_type, NULL);
        abd_assert(read_type == ABDT_SECTION);
        abd_read_string(buf, NULL);

    } break;

    case ABD_WRITE: {
        abd_assert(section_label != NULL);
        abd_write_field_header(buf, ABDT_SECTION, false);
        abd_write_string(buf, section_label);

    } break;
    }
}

void abd_transfer(int rw, uint8_t type, AbdBuffer* buf, void* data, char* write_annotation) {
    abd_assert(type < ABD_TYPE_COUNT);

    switch (rw) {
    case ABD_READ: {
        uint8_t read_type;
        abd_read_field(buf, &read_type, NULL);
        abd_assert(read_type != ABDT_SECTION);
        abd_assert(read_type == type);

        abd_data_read[type](buf, data);
    } break;

    case ABD_WRITE: {
        abd_write_field_header(buf, type, write_annotation);
        if (write_annotation) abd_write_string(buf, write_annotation);

        abd_data_write[type](buf, data);
    } break;
    }
}

void abd_read_field(AbdBuffer* buf, uint8_t* type, char** annotation) {
    uint8_t head = buf->bytes[buf->pos++];
    uint8_t read_type = head & ABD_TYPE_MASK;

    if (head & ABDF_ANNOTATED) {
        uint8_t annotation_length = buf->bytes[buf->pos++];
        if (annotation)
            *annotation = (char*)buf->bytes + buf->pos;
        buf->pos += annotation_length;
    }
    else if (annotation)
        *annotation = NULL;

    *type = read_type;
}

bool abd_inspect(AbdBuffer* buf, FILE* f) {
    int r = true;
    int old_pos = buf->pos;
    int limit;
    if (old_pos)
        limit = old_pos;
    else
        limit = buf->capacity;

    buf->pos = 0;

    while (buf->pos < limit) {
        uint8_t type;
        char* annotation;
        abd_read_field(buf, &type, &annotation);

        if (type != ABDT_SECTION)
            fprintf(f, "%s: ", abd_type_str(type));

        if (type < ABD_TYPE_COUNT)
            abd_data_inspect[type](buf, type, f);
        else if (type == ABDT_SECTION) {
            char section_text[512];
            abd_read_string(buf, section_text);
            fprintf(f, "==== %s ====", section_text);
        }
        else {
            fprintf(f, "Cannot inspect type: %i\nExiting inspection.\n", type);
            r = false;
            goto Done;
        }

        if (annotation) {
            fprintf(f, " -- \"%s\"", annotation);
        }
        fprintf(f, "\n");
    }

    Done:
    buf->pos = old_pos;
    return r;
}
