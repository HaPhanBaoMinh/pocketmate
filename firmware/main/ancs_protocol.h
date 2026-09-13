#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { ANCS_APP = 0, ANCS_TITLE = 1, ANCS_SUBTITLE = 2, ANCS_MESSAGE = 3 };
enum { ANCS_ADDED = 0, ANCS_MODIFIED = 1, ANCS_REMOVED = 2 };
#define ANCS_TITLE_MAX 128
#define ANCS_MESSAGE_MAX 384
#define ANCS_RESPONSE_MAX 1024

typedef struct { uint8_t event, flags, category, count; uint32_t uid; } ancs_event_t;
typedef struct {
    uint32_t uid;
    uint8_t expected_mask;
    size_t used;
    bool complete, failed;
    uint8_t data[ANCS_RESPONSE_MAX];
} ancs_response_t;

bool ancs_decode_event(const uint8_t *data, size_t len, ancs_event_t *event);
/* Only GetNotificationAttributes (command 0) is implemented: no actions. */
size_t ancs_request(uint8_t *dst, size_t capacity, uint32_t uid, bool text);
void ancs_response_begin(ancs_response_t *r, uint32_t uid, bool text);
/* 0=incomplete, 1=complete, -1=invalid. Handles arbitrary BLE fragmentation. */
int ancs_response_feed(ancs_response_t *r, const uint8_t *data, size_t len);
bool ancs_response_text(const ancs_response_t *r, uint8_t attr, char *dst, size_t capacity);
