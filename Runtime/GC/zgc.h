#ifndef Z_GC_H
#define Z_GC_H

#include <stddef.h>
#include <stdint.h>

typedef struct ZGCHeader {
    void* typeinfo;
    int32_t mark_flags;
    int32_t reserved;
} ZGCHeader;

/* Marks an object as never collectable. String literals live in static storage */
#define Z_GC_IMMORTAL 0x7FFFFFFF

void* z_gc_alloc(size_t size, void* typeinfo);

void z_abort(const char* message);

#endif
