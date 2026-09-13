#include "ancs_protocol.h"
#include <string.h>

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool ancs_decode_event(const uint8_t *p, size_t len, ancs_event_t *e)
{
    if (!p || !e || len != 8 || p[0] > ANCS_REMOVED) return false;
    *e = (ancs_event_t){p[0], p[1], p[2], p[3], le32(p + 4)};
    return true;
}

size_t ancs_request(uint8_t *p, size_t capacity, uint32_t uid, bool text)
{
    size_t n = text ? 14 : 6;
    if (!p || capacity < n) return 0;
    p[0] = 0;
    for (int i = 0; i < 4; i++) p[1 + i] = (uint8_t)(uid >> (8 * i));
    if (!text) p[5] = ANCS_APP;
    else {
        const uint16_t limits[] = {ANCS_TITLE_MAX, ANCS_TITLE_MAX, ANCS_MESSAGE_MAX};
        for (int i = 0; i < 3; i++) {
            p[5 + i * 3] = ANCS_TITLE + i;
            p[6 + i * 3] = limits[i] & 0xff;
            p[7 + i * 3] = limits[i] >> 8;
        }
    }
    return n;
}

void ancs_response_begin(ancs_response_t *r, uint32_t uid, bool text)
{
    memset(r, 0, sizeof(*r));
    r->uid = uid;
    r->expected_mask = text ? 0x0e : 0x01;
}

int ancs_response_feed(ancs_response_t *r, const uint8_t *data, size_t len)
{
    if (r->failed || r->complete || (!data && len) || len > sizeof(r->data) - r->used) goto invalid;
    if (len) memcpy(r->data + r->used, data, len);
    r->used += len;
    if (!r->used) return 0;
    if (r->data[0] != 0) goto invalid;
    if (r->used < 5) return 0;
    if (le32(r->data + 1) != r->uid) goto invalid;
    size_t pos = 5;
    uint8_t mask = 0;
    while (pos < r->used) {
        if (r->used - pos < 3) return 0;
        uint8_t id = r->data[pos];
        size_t n = (size_t)r->data[pos + 1] | (size_t)r->data[pos + 2] << 8;
        if (id > ANCS_MESSAGE || !(r->expected_mask & (1 << id)) || (mask & (1 << id))) goto invalid;
        size_t limit = id == ANCS_APP ? 255 : id == ANCS_MESSAGE ? ANCS_MESSAGE_MAX : ANCS_TITLE_MAX;
        if (n > limit) goto invalid;
        pos += 3;
        if (r->used - pos < n) return 0;
        pos += n;
        mask |= 1 << id;
        if (mask == r->expected_mask) {
            if (pos != r->used) goto invalid;
            r->complete = true;
            return 1;
        }
    }
    return 0;
invalid:
    r->failed = true;
    return -1;
}

bool ancs_response_text(const ancs_response_t *r, uint8_t attr, char *dst, size_t capacity)
{
    if (!dst || !capacity) return false;
    dst[0] = 0;
    if (!r->complete || r->failed) return false;
    for (size_t pos = 5; pos < r->used;) {
        uint8_t id = r->data[pos];
        size_t n = (size_t)r->data[pos + 1] | (size_t)r->data[pos + 2] << 8;
        pos += 3;
        if (id == attr) {
            /* Never silently truncate app IDs or UTF-8 strings. */
            if (n >= capacity) return false;
            memcpy(dst, r->data + pos, n);
            dst[n] = 0;
            return true;
        }
        pos += n;
    }
    return false;
}
