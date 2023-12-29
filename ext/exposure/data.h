// ABD stands for Annotated Binary Data/Document (whatever).

#ifndef ABD_DATA_H
#define ABD_DATA_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "abd_buffer.h"

#ifndef bool
#define bool int8_t
#endif
#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif

#define ABD_READ 0
#define ABD_WRITE 1

#define buf_new(bbytes, bpos) (AbdBuffer){.pos=(bpos), .capacity=INT_MAX, .bytes=(bbytes)}
#define buf_new_cap(bbytes, bpos, bcap) (AbdBuffer){.pos=(bpos), .capacity=(bcap), .bytes=(unsigned char*)(bbytes)}
#define DATA_CHUNK_TO_BUF(rw, chunk, bufname, write_capacity)\
    if ((rw) == ABD_WRITE) {\
        set_data_chunk_cap((chunk), (write_capacity));\
        (chunk)->size = 0;\
    }\
    else {\
        (chunk)->capacity = (chunk)->size;\
        (chunk)->size = 0;\
    }\
    AbdBuffer* bufname = (AbdBuffer*)(chunk);

#define ABDF_ANNOTATED (1 << 7)
#define ABD_TYPE_MASK (~(ABDF_ANNOTATED))
enum AbdType {
    ABDT_FLOAT,
    ABDT_VEC2,
    ABDT_VEC3,
    ABDT_VEC4,
    ABDT_S8,
    ABDT_S16,
    ABDT_S32,
    ABDT_S64,
    ABDT_U8,
    ABDT_U16,
    ABDT_U32,
    ABDT_U64,
    ABDT_COLOR,
    ABDT_BOOL,
    ABDT_STRING,
    ABD_TYPE_COUNT,
    ABDT_SECTION
};

static const char* abd_type_str(uint8_t type) {
    switch (type) {
    case ABDT_FLOAT:   return "Float";
    case ABDT_VEC2:    return "Vec2";
    case ABDT_VEC3:    return "Vec3";
    case ABDT_VEC4:    return "Vec4";
    case ABDT_S8:      return "Sint8";
    case ABDT_S16:     return "Sint16";
    case ABDT_S32:     return "Sint32";
    case ABDT_S64:     return "Sint64";
    case ABDT_U8:      return "Uint8";
    case ABDT_U16:     return "Uint16";
    case ABDT_U32:     return "Uint32";
    case ABDT_U64:     return "Uint64";
    case ABDT_COLOR:   return "RGBA Color";
    case ABDT_BOOL:    return "Boolean";
    case ABDT_STRING:  return "String";
    case ABDT_SECTION: return "(Section)";
    default:
        return "ERROR";
    }
}

struct abdvec2_t { float x, y; };
struct abdvec3_t { float x, y, z; };
struct abdvec4_t { float x, y, z, w; };
struct abdcolor_t { uint8_t r, g, b, a; };
#define ABDT_FLOAT_t   float
#define ABDT_VEC2_t    struct abdvec2_t
#define ABDT_VEC3_t    struct abdvec3_t
#define ABDT_VEC4_t    struct abdvec4_t
#define ABDT_S8_t      int8_t
#define ABDT_S16_t     int16_t
#define ABDT_S32_t     int32_t
#define ABDT_S64_t     int64_t
#define ABDT_U8_t      uint8_t
#define ABDT_U16_t     uint16_t
#define ABDT_U32_t     uint32_t
#define ABDT_U64_t     uint64_t
#define ABDT_COLOR_t   struct abdcolor_t
#define ABDT_BOOL_t    bool
#define ABDT_STRING_t  char*

typedef void(*DataFunc)(AbdBuffer*, void*);
typedef void(*DataInspectFunc)(AbdBuffer*, uint8_t, FILE* f);
extern DataFunc abd_data_write[];
extern DataFunc abd_data_read[];
extern DataInspectFunc abd_data_inspect[];

void abd_transfer(int rw, uint8_t type, AbdBuffer* buf, void* data, char* write_annotation);
void abd_section(int rw, AbdBuffer* buf, char* section_label);
void abd_read_field(AbdBuffer* buf, uint8_t* type, char** annotation);
bool abd_inspect(AbdBuffer* buf, FILE* f);

void abd_write_string(AbdBuffer* buf, void* string);
void abd_read_string(AbdBuffer* buf, void* dest);

#define data_section abd_section

#define data_float_a(rw, buf, data, write_annotation)  abd_transfer((rw), ABDT_FLOAT,  (buf), (data), (write_annotation))
#define data_vec2_a(rw, buf, data, write_annotation)   abd_transfer((rw), ABDT_VEC2,   (buf), (data), (write_annotation))
#define data_vec3_a(rw, buf, data, write_annotation)   abd_transfer((rw), ABDT_VEC3,   (buf), (data), (write_annotation))
#define data_vec4_a(rw, buf, data, write_annotation)   abd_transfer((rw), ABDT_VEC4,   (buf), (data), (write_annotation))
#define data_s8_a(rw, buf, data, write_annotation)     abd_transfer((rw), ABDT_S8,     (buf), (data), (write_annotation))
#define data_s16_a(rw, buf, data, write_annotation)    abd_transfer((rw), ABDT_S16,    (buf), (data), (write_annotation))
#define data_s32_a(rw, buf, data, write_annotation)    abd_transfer((rw), ABDT_S32,    (buf), (data), (write_annotation))
#define data_s64_a(rw, buf, data, write_annotation)    abd_transfer((rw), ABDT_S64,    (buf), (data), (write_annotation))
#define data_u8_a(rw, buf, data, write_annotation)     abd_transfer((rw), ABDT_U8,     (buf), (data), (write_annotation))
#define data_u16_a(rw, buf, data, write_annotation)    abd_transfer((rw), ABDT_U16,    (buf), (data), (write_annotation))
#define data_u32_a(rw, buf, data, write_annotation)    abd_transfer((rw), ABDT_U32,    (buf), (data), (write_annotation))
#define data_u64_a(rw, buf, data, write_annotation)    abd_transfer((rw), ABDT_U64,    (buf), (data), (write_annotation))
#define data_color_a(rw, buf, data, write_annotation)  abd_transfer((rw), ABDT_COLOR,  (buf), (data), (write_annotation))
#define data_bool_a(rw, buf, data, write_annotation)   abd_transfer((rw), ABDT_BOOL,   (buf), (data), (write_annotation))
#define data_string_a(rw, buf, data, write_annotation) abd_transfer((rw), ABDT_STRING, (buf), (data), (write_annotation))

#ifdef _DEBUG
#define IDENT_ANNOTATION(ident) #ident
#else
#define IDENT_ANNOTATION(ident) NULL
#endif

#define data_float(rw, buf, data)  abd_transfer((rw), ABDT_FLOAT,  (buf), (data), IDENT_ANNOTATION(data))
#define data_vec2(rw, buf, data)   abd_transfer((rw), ABDT_VEC2,   (buf), (data), IDENT_ANNOTATION(data))
#define data_vec3(rw, buf, data)   abd_transfer((rw), ABDT_VEC3,   (buf), (data), IDENT_ANNOTATION(data))
#define data_vec4(rw, buf, data)   abd_transfer((rw), ABDT_VEC4,   (buf), (data), IDENT_ANNOTATION(data))
#define data_s8(rw, buf, data)     abd_transfer((rw), ABDT_S8,     (buf), (data), IDENT_ANNOTATION(data))
#define data_s16(rw, buf, data)    abd_transfer((rw), ABDT_S16,    (buf), (data), IDENT_ANNOTATION(data))
#define data_s32(rw, buf, data)    abd_transfer((rw), ABDT_S32,    (buf), (data), IDENT_ANNOTATION(data))
#define data_s64(rw, buf, data)    abd_transfer((rw), ABDT_S64,    (buf), (data), IDENT_ANNOTATION(data))
#define data_u8(rw, buf, data)     abd_transfer((rw), ABDT_U8,     (buf), (data), IDENT_ANNOTATION(data))
#define data_u16(rw, buf, data)    abd_transfer((rw), ABDT_U16,    (buf), (data), IDENT_ANNOTATION(data))
#define data_u32(rw, buf, data)    abd_transfer((rw), ABDT_U32,    (buf), (data), IDENT_ANNOTATION(data))
#define data_u64(rw, buf, data)    abd_transfer((rw), ABDT_U64,    (buf), (data), IDENT_ANNOTATION(data))
#define data_color(rw, buf, data)  abd_transfer((rw), ABDT_COLOR,  (buf), (data), IDENT_ANNOTATION(data))
#define data_bool(rw, buf, data)   abd_transfer((rw), ABDT_BOOL,   (buf), (data), IDENT_ANNOTATION(data))
#define data_string(rw, buf, data) abd_transfer((rw), ABDT_STRING, (buf), (data), IDENT_ANNOTATION(data))

#endif //ABD_DATA_H
