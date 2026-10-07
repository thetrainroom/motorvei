/* SPDX-FileCopyrightText: 2026 Thierry Gschwind
 * SPDX-License-Identifier: Apache-2.0
 */
#include "playlist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Order matters: the first CM_CMD_COUNT names follow cm_cmd_t, the permanent variants come after
const char *const playlist_cmd_names[] = {
    "stop", "continue", "brake", "restore", "macro", "reverse", "reverse_magnet", "lane",
    "speed", "speed_min", "speed_max", "speed_default", "function_on", "function_off", "magnet",
    "rolling_highway", "raw",
    "function_on_permanent", "function_off_permanent",
    NULL,
};

bool playlist_cmd_from_name(const char *name, cm_command_t *out)
{
    memset(out, 0, sizeof(*out));
    for (int i = 0; playlist_cmd_names[i]; i++) {
        if (strcmp(name, playlist_cmd_names[i]) == 0) {
            if (i < CM_CMD_COUNT) {
                out->cmd = (cm_cmd_t)i;
            } else {
                out->cmd = (i == CM_CMD_COUNT) ? CM_CMD_FUNCTION_ON : CM_CMD_FUNCTION_OFF;
                out->permanent = true;
            }
            return true;
        }
    }
    return false;
}

const char *playlist_cmd_name(const cm_command_t *c)
{
    if (c->permanent && c->cmd == CM_CMD_FUNCTION_ON) return playlist_cmd_names[CM_CMD_COUNT];
    if (c->permanent && c->cmd == CM_CMD_FUNCTION_OFF) return playlist_cmd_names[CM_CMD_COUNT + 1];
    return (c->cmd < CM_CMD_COUNT) ? playlist_cmd_names[c->cmd] : "?";
}

const char *playlist_check_command(const cm_command_t *c)
{
    if (c->cmd == CM_CMD_RAW && c->raw_len == 0) {
        return "missing_raw";
    }
    cm_code_t codes[2];
    return cm_ir_codes(c, codes) ? NULL : "out_of_range";
}

const char *playlist_parse_text(const char *text, playlist_t *out)
{
    out->n = 0;
    char buf[PLAYLIST_TEXT_MAX + 1];
    if (strlen(text) >= sizeof(buf)) {
        return "too_long";
    }
    strcpy(buf, text);
    char *save = NULL;
    for (char *e = strtok_r(buf, ",", &save); e; e = strtok_r(NULL, ",", &save)) {
        while (*e == ' ') e++;
        for (char *end = e + strlen(e); end > e && end[-1] == ' ';) *--end = '\0';
        if (!*e) continue;
        if (out->n >= PLAYLIST_MAX) {
            return "too_many_entries";
        }
        bool short_range = false;
        char *slash = strchr(e, '/');
        if (slash) {
            if (strcmp(slash, "/s") != 0) return "bad_suffix";
            short_range = true;
            *slash = '\0';
        }
        char *colon = strchr(e, ':');
        if (colon) *colon = '\0';
        for (char *end = e + strlen(e); end > e && end[-1] == ' ';) *--end = '\0';
        if (strcmp(e, "dark") == 0) {
            if (colon || short_range) return "bad_arg";
            continue;                   // sends nothing; "dark" alone is an empty playlist
        }
        cm_command_t *c = &out->cmd[out->n];
        const char *name = strcmp(e, "go") == 0 ? "continue" : e;     // "go" is the continue command (BE 00)
        if (!playlist_cmd_from_name(name, c)) {
            return "unknown_cmd";
        }
        c->short_range = short_range;
        if (colon) {
            if (c->cmd == CM_CMD_RAW) {
                c->raw_len = cm_ir_parse_hex(colon + 1, c->raw, sizeof(c->raw));
            } else {
                char *end = NULL;
                c->arg = (int)strtol(colon + 1, &end, 10);
                if (!end || *end) return "bad_arg";
            }
        }
        const char *reason = playlist_check_command(c);
        if (reason) return reason;
        out->n++;
    }
    return NULL;
}

size_t playlist_expand(const playlist_t *p, cm_code_t *codes, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; i < p->n; i++) {
        cm_code_t two[2];
        size_t k = cm_ir_codes(&p->cmd[i], two);
        for (size_t j = 0; j < k && n < max; j++) {
            codes[n++] = two[j];
        }
    }
    return n;
}

// Commands where arg 0 is a value of its own (function 0, lane 0, magnet N, speed 0, exit), printed as ":0"
static bool arg_zero_meaningful(cm_cmd_t cmd)
{
    return cmd == CM_CMD_LANE || cmd == CM_CMD_SPEED || cmd == CM_CMD_FUNCTION_ON || cmd == CM_CMD_FUNCTION_OFF ||
           cmd == CM_CMD_MAGNET || cmd == CM_CMD_ROLLING_HIGHWAY;
}

size_t playlist_to_text(const playlist_t *p, char *out, size_t size)
{
    size_t n = 0;
    out[0] = '\0';
    for (size_t i = 0; i < p->n && n < size; i++) {
        const cm_command_t *c = &p->cmd[i];
        n += (size_t)snprintf(out + n, size - n, "%s%s", i ? "," : "", playlist_cmd_name(c));
        if (c->cmd == CM_CMD_RAW) {
            n += (size_t)snprintf(out + n, size - n, ":");
            for (size_t k = 0; k < c->raw_len && n < size; k++) {
                n += (size_t)snprintf(out + n, size - n, "%02X", c->raw[k]);
            }
        } else if (c->arg || arg_zero_meaningful(c->cmd)) {
            n += (size_t)snprintf(out + n, size - n, ":%d", c->arg);
        }
        if (c->short_range && n < size) {
            n += (size_t)snprintf(out + n, size - n, "/s");
        }
    }
    return n < size ? n : size - 1;
}
