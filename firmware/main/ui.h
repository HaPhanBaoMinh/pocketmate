/* Screen model and layout for the Pocketmate LCD. Pure C, host-testable. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"
#include "ancs_protocol.h"

typedef enum {
    MANEUVER_NONE, MANEUVER_STRAIGHT, MANEUVER_LEFT, MANEUVER_RIGHT, MANEUVER_SLIGHT_LEFT,
    MANEUVER_SLIGHT_RIGHT, MANEUVER_UTURN, MANEUVER_ROUNDABOUT, MANEUVER_ARRIVE,
} maneuver_t;

typedef struct {
    char status[48];                 /* one line in the top bar */
    char passkey[8];                 /* six digits while the iPhone asks for a pairing code */
    bool has_maps;                   /* title/subtitle/message hold a Google Maps notification */
    bool stale;                      /* link lost after the last Maps update */
    uint32_t age_seconds;            /* seconds since the last Maps update */
    unsigned other_count;            /* notifications from other apps (content never fetched) */
    char other_app[64];              /* bundle id of the last such notification */
    char title[ANCS_TITLE_MAX + 1];
    char subtitle[ANCS_TITLE_MAX + 1];
    char message[ANCS_MESSAGE_MAX + 1];
} ui_model_t;

void ui_render_band(const ui_model_t *model, gfx_band_t *band);
/* Vietnamese Google Maps instruction text -> maneuver; distance like "200 m"/"1,2 km" copied
 * into `distance` (may be empty). Pure function, exposed for tests. */
maneuver_t ui_parse_instruction(const char *message, char *distance, size_t distance_capacity);
/* Splits "…, sau đó rẽ trái…" / "…rồi rẽ trái…" into the current step and the following one.
 * `next` receives the text after the connector (may be empty). Returns the length of the
 * current step in bytes. */
size_t ui_split_next(const char *message, const char **next);
