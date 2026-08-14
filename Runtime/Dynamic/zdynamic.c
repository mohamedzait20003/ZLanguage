#include "Dynamic/zdynamic.h"

#include <stdio.h>
#include <string.h>

static const char* tag_name(int32_t tag) {
    switch (tag) {
        case DYN_INT:    return "int";
        case DYN_FLOAT:  return "float";
        case DYN_DOUBLE: return "double";
        case DYN_BOOL:   return "bool";
        case DYN_CHAR:   return "character";
        case DYN_STRING: return "string";
        default:         return "<invalid>";
    }
}

ZDynamic* z_dynamic_box(int32_t tag, int64_t data) {
    ZDynamic* d = (ZDynamic*)z_gc_alloc(sizeof(ZDynamic), NULL);
    d->tag  = tag;
    d->data = data;
    return d;
}

int64_t z_dynamic_unbox(int32_t expected, const ZDynamic* d) {
    if (!d)
        z_abort("static_cast from a null dynamic value");

    if (d->tag != expected) {
        char message[128];
        snprintf(message, sizeof(message), "static_cast from dynamic holding '%s' to '%s'", tag_name(d->tag), tag_name(expected));
        z_abort(message);
    }

    return d->data;
}

int64_t z_dynamic_try_unbox(int32_t expected, const ZDynamic* d) {
    if (!d || d->tag != expected)
        return 0;

    return d->data;
}

void z_dynamic_print(const ZDynamic* d) {
    if (!d) {
        printf("null\n");
        return;
    }

    switch (d->tag) {
        case DYN_INT:
            printf("%lld\n", (long long)d->data);
            break;

        case DYN_FLOAT: {
            uint32_t bits = (uint32_t)d->data;
            float value;
            memcpy(&value, &bits, sizeof(value));
            printf("%f\n", (double)value);
            break;
        }

        case DYN_DOUBLE: {
            uint64_t bits = (uint64_t)d->data;
            double value;
            memcpy(&value, &bits, sizeof(value));
            printf("%lf\n", value);
            break;
        }

        case DYN_BOOL:
            printf("%d\n", d->data != 0 ? 1 : 0);
            break;

        case DYN_CHAR:
            printf("%c\n", (int)d->data);
            break;

        case DYN_STRING: {
            const ZString* s = (const ZString*)(intptr_t)d->data;
            printf("%s\n", s ? z_string_cstr(s) : "null");
            break;
        }

        default:
            z_abort("print of a dynamic with an invalid type tag");
    }
}
