#include "GC/zgc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void* z_gc_alloc(size_t size, void* typeinfo) {
    void* raw = malloc(size);

    if (!raw)
        z_abort("out of memory");

    memset(raw, 0, size);

    ((ZGCHeader*)raw)->typeinfo = typeinfo;
    return raw;
}

void z_abort(const char* message) {
    fprintf(stderr, "Z runtime error: %s\n", message);
    fflush(stderr);
    abort();
}
