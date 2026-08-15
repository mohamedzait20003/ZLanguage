#include "String/zstring.h"

#include <string.h>

/* -- access -- */

int64_t z_string_length(const ZString* s) {
    if (!s)
        z_abort("length() of a null string");

    return s->length;
}

int32_t z_string_is_empty(const ZString* s) {
    return z_string_length(s) == 0;
}

int32_t z_string_get(const ZString* s, int64_t index) {
    if (!s)
        z_abort("get() on a null string");

    if (index < 0 || index >= s->length)
        z_abort("string index out of range");

    return (int32_t)s->bytes[index];
}

/* -- search --- */

/* Byte offset of the first `needle` at or after `from`, or -1. An empty needle
 * matches at `from`, which is what makes replace() and count() terminate. */
static int64_t find_from(const ZString* haystack, const ZString* needle, int64_t from) {
    if (needle->length == 0)
        return from <= haystack->length ? from : -1;

    if (needle->length > haystack->length)
        return -1;

    for (int64_t i = from; i + needle->length <= haystack->length; ++i)
        if (memcmp(haystack->bytes + i, needle->bytes, (size_t)needle->length) == 0)
            return i;

    return -1;
}

static void require_two(const ZString* a, const ZString* b, const char* what) {
    if (!a || !b)
        z_abort(what);
}

int64_t z_string_index_of(const ZString* s, const ZString* sub) {
    require_two(s, sub, "index_of() on a null string");
    return find_from(s, sub, 0);
}

int64_t z_string_last_index_of(const ZString* s, const ZString* sub) {
    require_two(s, sub, "last_index_of() on a null string");

    if (sub->length == 0)
        return s->length;

    for (int64_t i = s->length - sub->length; i >= 0; --i)
        if (memcmp(s->bytes + i, sub->bytes, (size_t)sub->length) == 0)
            return i;

    return -1;
}

int32_t z_string_contains(const ZString* s, const ZString* sub) {
    return z_string_index_of(s, sub) >= 0;
}

int32_t z_string_starts_with(const ZString* s, const ZString* prefix) {
    require_two(s, prefix, "starts_with() on a null string");

    if (prefix->length > s->length)
        return 0;

    return memcmp(s->bytes, prefix->bytes, (size_t)prefix->length) == 0;
}

int32_t z_string_ends_with(const ZString* s, const ZString* suffix) {
    require_two(s, suffix, "ends_with() on a null string");

    if (suffix->length > s->length)
        return 0;

    return memcmp(s->bytes + (s->length - suffix->length), suffix->bytes,
                  (size_t)suffix->length) == 0;
}

int64_t z_string_count(const ZString* s, const ZString* sub) {
    require_two(s, sub, "count() on a null string");

    /* An empty needle would match forever; report 0 rather than looping. */
    if (sub->length == 0)
        return 0;

    int64_t total = 0;
    for (int64_t i = 0; (i = find_from(s, sub, i)) >= 0; i += sub->length)
        ++total;

    return total;
}

/* -- slice --- */

ZString* z_string_slice(const ZString* s, int64_t start, int64_t end) {
    if (!s)
        z_abort("slice() of a null string");

    if (start < 0 || end > s->length || start > end)
        z_abort("slice() range out of bounds");

    return z_string_from_bytes(s->bytes + start, end - start);
}

/* -- case --- */

static ZString* map_ascii(const ZString* s, int upper) {
    if (!s)
        z_abort("case conversion of a null string");

    ZString* out = z_string_alloc(s->length);

    for (int64_t i = 0; i < s->length; ++i) {
        uint8_t c = s->bytes[i];

        if (upper && c >= 'a' && c <= 'z')
            c = (uint8_t)(c - 'a' + 'A');
        else if (!upper && c >= 'A' && c <= 'Z')
            c = (uint8_t)(c - 'A' + 'a');

        out->bytes[i] = c;
    }

    return out;
}

ZString* z_string_to_upper(const ZString* s) { return map_ascii(s, 1); }
ZString* z_string_to_lower(const ZString* s) { return map_ascii(s, 0); }

/* -- whitespace -- */

static int is_space(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static ZString* trim_range(const ZString* s, int from_start, int from_end) {
    if (!s)
        z_abort("trim() of a null string");

    int64_t begin = 0, end = s->length;

    if (from_start)
        while (begin < end && is_space(s->bytes[begin])) ++begin;

    if (from_end)
        while (end > begin && is_space(s->bytes[end - 1])) --end;

    return z_string_from_bytes(s->bytes + begin, end - begin);
}

ZString* z_string_trim(const ZString* s)       { return trim_range(s, 1, 1); }
ZString* z_string_trim_start(const ZString* s) { return trim_range(s, 1, 0); }
ZString* z_string_trim_end(const ZString* s)   { return trim_range(s, 0, 1); }

/* -- transformation -- */

ZString* z_string_replace(const ZString* s, const ZString* old_sub, const ZString* new_sub) {
    if (!s || !old_sub || !new_sub)
        z_abort("replace() on a null string");

    if (old_sub->length == 0)
        return z_string_from_bytes(s->bytes, s->length);

    const int64_t hits = z_string_count(s, old_sub);
    if (hits == 0)
        return z_string_from_bytes(s->bytes, s->length);

    ZString* out = z_string_alloc(s->length + hits * (new_sub->length - old_sub->length));

    int64_t read = 0, write = 0;
    for (int64_t at; (at = find_from(s, old_sub, read)) >= 0; read = at + old_sub->length) {
        memcpy(out->bytes + write, s->bytes + read, (size_t)(at - read));
        write += at - read;

        memcpy(out->bytes + write, new_sub->bytes, (size_t)new_sub->length);
        write += new_sub->length;
    }

    memcpy(out->bytes + write, s->bytes + read, (size_t)(s->length - read));
    return out;
}

ZString* z_string_repeat(const ZString* s, int64_t times) {
    if (!s)
        z_abort("repeat() of a null string");

    if (times < 0)
        z_abort("repeat() count cannot be negative");

    ZString* out = z_string_alloc(s->length * times);

    for (int64_t i = 0; i < times; ++i)
        memcpy(out->bytes + i * s->length, s->bytes, (size_t)s->length);

    return out;
}

static ZString* pad(const ZString* s, int64_t width, int32_t ch, int on_left) {
    if (!s)
        z_abort("pad() of a null string");

    /* Already at or past the requested width: return a copy, never a truncation. */
    if (width <= s->length)
        return z_string_from_bytes(s->bytes, s->length);

    ZString* out = z_string_alloc(width);
    const int64_t fill = width - s->length;

    if (on_left) {
        memset(out->bytes, (int)(uint8_t)ch, (size_t)fill);
        memcpy(out->bytes + fill, s->bytes, (size_t)s->length);
    } else {
        memcpy(out->bytes, s->bytes, (size_t)s->length);
        memset(out->bytes + s->length, (int)(uint8_t)ch, (size_t)fill);
    }

    return out;
}

ZString* z_string_pad_left(const ZString* s, int64_t width, int32_t ch)  { return pad(s, width, ch, 1); }
ZString* z_string_pad_right(const ZString* s, int64_t width, int32_t ch) { return pad(s, width, ch, 0); }

/* -- conversion --- */

ZString* z_string_from_character(int32_t ch) {
    if (ch < 0 || ch > 0x7F)
        z_abort("from_character() supports ASCII code points only");

    uint8_t byte = (uint8_t)ch;
    return z_string_from_bytes(&byte, 1);
}

ZString* z_string_from_int(int64_t value) {
    /* Widest int64 is "-9223372036854775808" — 20 characters. */
    char buffer[24];
    int written = 0;

    if (value == 0) {
        buffer[written++] = '0';
    } else {
        /* Negate into unsigned so INT64_MIN does not overflow. */
        uint64_t magnitude = value < 0 ? (uint64_t)0 - (uint64_t)value : (uint64_t)value;

        while (magnitude > 0) {
            buffer[written++] = (char)('0' + (magnitude % 10));
            magnitude /= 10;
        }

        if (value < 0)
            buffer[written++] = '-';
    }

    for (int i = 0; i < written / 2; ++i) {
        char tmp = buffer[i];
        buffer[i] = buffer[written - 1 - i];
        buffer[written - 1 - i] = tmp;
    }

    return z_string_from_bytes((const uint8_t*)buffer, written);
}

ZString* z_string_from_bool(int32_t value) {
    const char* text = value ? "true" : "false";
    return z_string_from_bytes((const uint8_t*)text, (int64_t)strlen(text));
}
