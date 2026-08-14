#ifndef Z_DYNAMIC_H
#define Z_DYNAMIC_H

#include "GC/zgc.h"
#include "String/zstring.h"

#include <stdint.h>

#define DYN_INT 1
#define DYN_FLOAT 2
#define DYN_DOUBLE 3
#define DYN_BOOL 4
#define DYN_CHAR 5
#define DYN_STRING 6

typedef struct ZDynamic {
    ZGCHeader gc_header;
    int32_t tag;
    int32_t padding;
    int64_t data;
} ZDynamic;

/* Boxing. Float and double payloads are bit-cast into `data` by the caller,
 * so one entry point covers every primitive. */
ZDynamic* z_dynamic_box(int32_t tag, int64_t data);

/* Unboxing for static_cast: aborts if the stored tag is not `expected`. */
int64_t z_dynamic_unbox(int32_t expected, const ZDynamic* d);

/* Unboxing for dynamic_cast: returns 0 on tag mismatch instead of aborting.
 * 0 is also the null pointer for DYN_STRING targets. */
int64_t z_dynamic_try_unbox(int32_t expected, const ZDynamic* d);

/* print(dynamic) — dispatches on the tag to the right printf format. */
void z_dynamic_print(const ZDynamic* d);

#endif
