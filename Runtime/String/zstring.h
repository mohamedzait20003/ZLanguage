#ifndef Z_STRING_H
#define Z_STRING_H

#include "GC/zgc.h"

#include <stdint.h>

typedef struct ZString {
    ZGCHeader header;
    int64_t length;
    uint8_t bytes[];
} ZString;

/* Allocates an uninitialised ZString with room for `length` bytes. */
ZString* z_string_alloc(int64_t length);

/* Allocates and copies `length` bytes from `bytes`. */
ZString* z_string_from_bytes(const uint8_t* bytes, int64_t length);

/* Returns a NUL-terminated copy for printf and other C APIs. The result is
 * owned by the runtime; callers must not free it. */
const char* z_string_cstr(const ZString* s);

/* `a + b` — allocates a new ZString holding the concatenation. */
ZString* z_string_concat(const ZString* a, const ZString* b);

int32_t z_string_cmp(const ZString* a, const ZString* b);

#endif
