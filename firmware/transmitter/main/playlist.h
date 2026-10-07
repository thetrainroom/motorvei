/* SPDX-FileCopyrightText: 2026 Thierry Gschwind
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * A channel's playlist: up to PLAYLIST_MAX commands, repeated round-robin. Two representations:
 *   - records, as MRRoIP object values: [{"cmd":"speed","arg":50,"range":"normal"}, {"cmd":"stop"}]
 *   - text, for presets (at most PLAYLIST_TEXT_MAX characters):
 *       "speed:50,stop"      entries separated by ',', "name[:arg][/s]", /s = short range, raw:<hex>
 *       "go" is accepted for "continue"; "dark" (alone) is an empty playlist — the channel sends nothing
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "cm_ir.h"

#define PLAYLIST_MAX        8
#define PLAYLIST_TEXT_MAX   120

typedef struct {
    cm_command_t cmd[PLAYLIST_MAX];
    size_t n;
} playlist_t;

// Command names, in cm_cmd_t order plus the permanent function variants; NULL-terminated (for /definition)
extern const char *const playlist_cmd_names[];

// Name <-> command. "function_on_permanent" maps to CM_CMD_FUNCTION_ON with permanent = true.
bool playlist_cmd_from_name(const char *name, cm_command_t *out);
const char *playlist_cmd_name(const cm_command_t *c);

// Parse the text form. Returns NULL on success, otherwise a reason (MRRoIP details[] wording).
const char *playlist_parse_text(const char *text, playlist_t *out);

// Text form of a playlist (the inverse of playlist_parse_text). Returns the length.
size_t playlist_to_text(const playlist_t *p, char *out, size_t size);

// Validate one command (arguments in range, raw bytes present). Returns NULL or a reason.
const char *playlist_check_command(const cm_command_t *c);

// All codes of the playlist in order (a command gives 1 or 2 codes). Returns the count.
size_t playlist_expand(const playlist_t *p, cm_code_t *codes, size_t max);
