/* Screen model and layout for the Pocketmate LCD. Pure C, host-testable. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"
#include "ancs_protocol.h"

typedef struct {
    char status[48];                 /* one line in the top bar */
    char passkey[8];                 /* six digits while the iPhone asks for a pairing code */
    bool has_maps;                   /* title/subtitle/message hold a Google Maps notification */
    bool stale;                      /* link lost after the last Maps update */
    uint32_t age_seconds;            /* seconds since the last Maps update */
    unsigned other_count;            /* notifications from other apps (content never fetched) */
    char title[ANCS_TITLE_MAX + 1];
    char subtitle[ANCS_TITLE_MAX + 1];
    char message[ANCS_MESSAGE_MAX + 1];
} ui_model_t;

void ui_render_band(const ui_model_t *model, gfx_band_t *band);
