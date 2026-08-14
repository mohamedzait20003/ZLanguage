#include "String/zstring.h"

#include <string.h>

ZString* z_string_alloc(int64_t length) {
    if (length < 0)
        z_abort("string length cannot be negative");

    ZString* s = (ZString*)z_gc_alloc(sizeof(ZString) + (size_t)length + 1, NULL);
    s->length = length;
    return s;
}

ZString* z_string_from_bytes(const uint8_t* bytes, int64_t length) {
    ZString* s = z_string_alloc(length);

    if (length > 0)
        memcpy(s->bytes, bytes, (size_t)length);

    return s;
}

const char* z_string_cstr(const ZString* s) {
    if (!s)
        z_abort("null string dereferenced");

    return (const char*)s->bytes;
}

ZString* z_string_concat(const ZString* a, const ZString* b) {
    if (!a || !b)
        z_abort("null string in concatenation");

    ZString* out = z_string_alloc(a->length + b->length);

    if (a->length > 0)
        memcpy(out->bytes, a->bytes, (size_t)a->length);

    if (b->length > 0)
        memcpy(out->bytes + a->length, b->bytes, (size_t)b->length);

    return out;
}

int32_t z_string_cmp(const ZString* a, const ZString* b) {
    if (!a || !b)
        z_abort("null string in comparison");

    const int64_t shorter = a->length < b->length ? a->length : b->length;

    if (shorter > 0) {
        const int diff = memcmp(a->bytes, b->bytes, (size_t)shorter);

        if (diff != 0)
            return diff < 0 ? -1 : 1;
    }

    if (a->length == b->length)
        return 0;

    return a->length < b->length ? -1 : 1;
}
