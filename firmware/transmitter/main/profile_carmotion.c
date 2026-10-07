/* SPDX-FileCopyrightText: 2026 Thierry Gschwind
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * MRRoIP profile "carmotion" 0.3 (PROFILE-CARMOTION.md): IR output channels sending Viessmann CarMotion commands to
 * vehicles. Two ways to drive them, usable together:
 *   - direct:  the master says what a channel sends:    {"ch1": [{"cmd": "speed", "arg": 30}, {"cmd": "stop"}]}
 *   - profile: stored channel commands, called by name or number:                     {"profile": "ns_go"}
 * A profile is a stored control message: a name and "actions", the shape of "objects" in /control. It sets the
 * channels it names and leaves the others as they are, so unrelated uses on one module do not disturb each other,
 * and the same profile names can be used on several modules with different channels.
 * Without a master (at start, after release or timeout) all channels go dark and the start profile is applied —
 * the autonomous programme of a stationary device (MRROIP-1.md §11.3).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ir_tx.h"
#include "mrroip_profile.h"
#include "playlist.h"

static const char *TAG = "carmotion";

#define CHANNELS        IR_TX_MAX_CHANNELS
#define PROFILES_MAX    32
#define PROFILE_NAME_MAX 16
static const int channel_gpio[CHANNELS] = { 10, 1 };   // ESP-C3-12F-Kit: IO3/4/5 RGB LED, IO6/7 OLED, IO8/9 strapping
static const char *const channel_ids[CHANNELS] = { "ch1", "ch2" };
static const char *const channel_ids_z[] = { "ch1", "ch2", NULL };  // the same, NULL-terminated
_Static_assert(CHANNELS == 2, "channel_ids and channel_ids_z list one entry per channel");

static const char *const modes[] = { "transmit", "off", NULL };

static const profile_info_t info = {
    .device_type = "carmotion",         // also the default device_name and mDNS host: no underscore
    .device_class = "stationary",
    .profile_version = "0.3",
    .modes = modes,
    .target_modes = NULL,
    .rest_mode = "transmit",
    .autonomous = true,
    .telemetry_hz = 0,
};

static const param_field_t profile_fields[] = {
    { .name = "profile", .type = PARAM_STRING, .max_len = PROFILE_NAME_MAX,
      .doc = "What a master calls, e.g. ns_go. The same name may be used on other modules." },
    { .name = "actions", .type = PARAM_ACTIONS, .values = channel_ids_z,
      .doc = "Channel -> commands, as in a control message: {\"ch1\": [{\"cmd\": \"continue\"}]}; [] = dark; "
             "a channel left out keeps what it sends" },
};

static const param_field_t channel_fields[] = {
    { .name = "name", .type = PARAM_STRING, .max_len = 24, .doc = "Where the LED is, e.g. Bus stop Nord" },
    { .name = "long_flash_us", .type = PARAM_INT, .default_int = 26, .min = 5, .max = 60, .unit = "us",
      .doc = "Normal-range flash width (original: transmitter power 25 -> 26 us)" },
    { .name = "short_flash_us", .type = PARAM_INT, .default_int = 21, .min = 5, .max = 60, .unit = "us",
      .doc = "Short-range flash width (original: 25 - 5 -> 21 us)" },
};

static param_desc_t params[] = {
    { .name = "profiles", .type = PARAM_RECORDS, .fields = profile_fields, .field_count = 2, .max_count = PROFILES_MAX,
      .default_json = "[{\"profile\":\"red\",\"actions\":{\"ch1\":[{\"cmd\":\"stop\"}]}},"
                      "{\"profile\":\"green\",\"actions\":{\"ch1\":[{\"cmd\":\"continue\"}]}}]",
      .doc = "Profile n is row n. A master calls one by name or number; it applies its actions." },
    { .name = "start_profile", .type = PARAM_STRING, .max_len = PROFILE_NAME_MAX, .default_str = "",
      .doc = "Profile (name or number) applied at start and when no master is present; empty = all dark" },
    { .name = "channels", .type = PARAM_RECORDS, .fields = channel_fields, .field_count = 3, .count = CHANNELS,
      .doc = "One row per IR output" },
    { .name = "gap_min_us", .type = PARAM_INT, .default_int = 5000, .min = 1000, .max = 30000, .unit = "us",
      .doc = "Darkness after each frame: random between gap_min_us and gap_max_us." },
    { .name = "gap_max_us", .type = PARAM_INT, .default_int = 15000, .min = 1000, .max = 30000, .unit = "us" },
};

typedef struct {
    char name[25];
    playlist_t playlist;        // what the channel sends now
    const char *error;          // the profile cell did not parse (channel left dark), or NULL
    bool dirty;                 // playlist changed, not yet handed to ir_tx
} channel_t;

static const char *value_to_playlist(const object_value_t *v, playlist_t *p);
static SemaphoreHandle_t lock;
static channel_t ch_state[CHANNELS];
static char last_profile[PROFILE_NAME_MAX + 1];    // name of the profile applied last, "" after a direct change
static bool from_master;
static bool transmitting = true;
static bool estopped;

const profile_info_t *profile_info(void)
{
    return &info;
}

const param_desc_t *profile_params(size_t *count)
{
    *count = sizeof(params) / sizeof(params[0]);
    return params;
}

const char *profile_etag(void)
{
    return "c3-2ch";
}

const char *profile_fault(void)
{
    return NULL;
}

// A master repeats its message: the same playlist again must not restart the round-robin
static void set_playlist_locked(channel_t *c, const playlist_t *p, const char *error)
{
    char a[PLAYLIST_TEXT_MAX + 1], b[PLAYLIST_TEXT_MAX + 1];
    playlist_to_text(&c->playlist, a, sizeof(a));
    playlist_to_text(p, b, sizeof(b));
    if (strcmp(a, b) != 0) {
        c->playlist = *p;
        c->dirty = true;
    }
    c->error = error;
}

static void push_locked(void)
{
    for (int ch = 0; ch < CHANNELS; ch++) {
        if (ch_state[ch].dirty) {
            cm_code_t codes[IR_TX_MAX_CODES];
            size_t n = playlist_expand(&ch_state[ch].playlist, codes, IR_TX_MAX_CODES);
            ir_tx_set_playlist(ch, codes, n);
            ch_state[ch].dirty = false;
        }
    }
    ir_tx_set_enabled(transmitting && !estopped);
}

// The profile a reference names: a name (the first row with it), or a number n for row n — also as text, from
// start_profile. NULL if there is none.
static const object_value_t *find_profile(const object_value_t *profiles, const char *name, long number)
{
    if (name) {
        char *end = NULL;
        long n = strtol(name, &end, 10);
        if (!*name) return NULL;
        if (*end) {
            for (int i = 0; i < value_array_size(profiles); i++) {
                const object_value_t *row = value_array_at(profiles, i);
                const char *row_name = value_str(value_member(row, "profile"));
                if (row_name && strcmp(row_name, name) == 0) return row;
            }
            return NULL;
        }
        number = n;
    }
    return number >= 1 ? value_array_at(profiles, (int)number - 1) : NULL;
}

// Apply a profile: every channel its actions name takes those commands; the others keep theirs
static void apply_profile_locked(const char *ref, long number)
{
    object_value_t *profiles = params_get_value("profiles");
    const object_value_t *row = find_profile(profiles, ref, number);
    if (row) {
        const char *name = value_str(value_member(row, "profile"));
        if (name && *name) {
            snprintf(last_profile, sizeof(last_profile), "%s", name);
        } else {
            snprintf(last_profile, sizeof(last_profile), "%ld", ref ? atol(ref) : number);
        }
        const object_value_t *actions = value_member(row, "actions");
        for (int ch = 0; ch < CHANNELS; ch++) {
            const object_value_t *v = value_member(actions, channel_ids[ch]);
            if (!v) continue;
            playlist_t p;
            const char *error = value_to_playlist(v, &p);     // checked when stored; kept for safety
            if (error) {
                ESP_LOGE(TAG, "profile %s %s: %s — channel dark", last_profile, channel_ids[ch], error);
                p.n = 0;
            }
            set_playlist_locked(&ch_state[ch], &p, error);
        }
    }
    params_value_free(profiles);
}

static void load_channels_locked(void)
{
    object_value_t *channels = params_get_value("channels");
    for (int ch = 0; ch < CHANNELS; ch++) {
        const object_value_t *rec = value_array_at(channels, ch);
        const char *name = value_str(value_member(rec, "name"));
        long lng = 26, shrt = 21;
        value_int(value_member(rec, "long_flash_us"), &lng);
        value_int(value_member(rec, "short_flash_us"), &shrt);
        snprintf(ch_state[ch].name, sizeof(ch_state[ch].name), "%s", name ? name : "");
        ir_tx_set_flash(ch, (uint16_t)lng, (uint16_t)shrt);
    }
    params_value_free(channels);
}

// Rest: all channels dark, then the start profile
static void load_rest_locked(void)
{
    const playlist_t dark = { .n = 0 };
    for (int ch = 0; ch < CHANNELS; ch++) {
        set_playlist_locked(&ch_state[ch], &dark, NULL);
    }
    last_profile[0] = '\0';
    char start[PROFILE_NAME_MAX + 1];
    params_get_str("start_profile", start, sizeof(start));
    if (*start) {
        apply_profile_locked(start, 0);
    }
    from_master = false;
}

static void apply_timing(void)
{
    ir_tx_timing_t t = {
        .gap_min_us = (uint32_t)params_get_int("gap_min_us"),
        .gap_max_us = (uint32_t)params_get_int("gap_max_us"),
    };
    if (t.gap_max_us < t.gap_min_us) {
        t.gap_max_us = t.gap_min_us;
    }
    ir_tx_set_timing(&t);
}

void profile_start(void)
{
    lock = xSemaphoreCreateMutex();
    for (int ch = 0; ch < CHANNELS; ch++) {
        ir_tx_start(ch, channel_gpio[ch]);
    }
    apply_timing();
    xSemaphoreTake(lock, portMAX_DELAY);
    load_channels_locked();
    load_rest_locked();
    push_locked();
    xSemaphoreGive(lock);
}

void profile_param_changed(const char *name)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (strcmp(name, "profiles") == 0 || strcmp(name, "start_profile") == 0) {
        // Without a master the rest state follows the new table; a master's next repeat picks it up itself
        if (!from_master) {
            load_rest_locked();
        }
    } else if (strcmp(name, "channels") == 0) {
        load_channels_locked();
    } else {
        apply_timing();
    }
    push_locked();
    xSemaphoreGive(lock);
}

static void emit_playlist_decl(emit_t *form)
{
    emit_str(form, "type", "object[]");
    emit_int(form, "max_count", PLAYLIST_MAX);
    emit_t *fields = emit_array(form, "fields");

    emit_t *cmd = emit_object(fields, NULL);
    emit_str(cmd, "name", "cmd");
    emit_str(cmd, "type", "enum");
    emit_t *values = emit_array(cmd, "values");
    for (int i = 0; playlist_cmd_names[i]; i++) {
        emit_str(values, NULL, playlist_cmd_names[i]);
    }
    emit_str(cmd, "default", "stop");

    emit_t *arg = emit_object(fields, NULL);
    emit_str(arg, "name", "arg");
    emit_str(arg, "type", "int");
    emit_int(arg, "min", 0);
    emit_int(arg, "max", 255);
    emit_int(arg, "default", 0);
    emit_str(arg, "doc", "speed: km/h; function: 0-11; macro: 1-14; lane: 0-7; magnet: pattern 0-7; "
                         "reverse: distance in cm; rolling_highway: 0 exit, 1 enter, 2 enter packed");

    emit_t *range = emit_object(fields, NULL);
    emit_str(range, "name", "range");
    emit_str(range, "type", "enum");
    emit_t *rv = emit_array(range, "values");
    emit_str(rv, NULL, "normal");
    emit_str(rv, NULL, "short");
    emit_str(range, "default", "normal");

    emit_t *raw = emit_object(fields, NULL);
    emit_str(raw, "name", "raw");
    emit_str(raw, "type", "string");
    emit_int(raw, "max_len", 2 * (CM_IR_MAX_DATA - 1) + CM_IR_MAX_DATA);
    emit_str(raw, "default", "");
}

// Objects (MRROIP-1.md §7.3): per channel its commands; and "profile", a profile to apply, by number or name
void profile_emit_objects(emit_t *objects)
{
    for (int ch = 0; ch < CHANNELS; ch++) {
        emit_t *o = emit_object(objects, NULL);
        emit_str(o, "id", channel_ids[ch]);
        emit_str(o, "doc", "What this channel sends, repeated in order; [] = dark");
        emit_playlist_decl(o);
    }
    emit_t *o = emit_object(objects, NULL);
    emit_str(o, "id", "profile");
    emit_str(o, "doc", "Apply a stored profile: the channels it has rows for take its commands, the others keep "
                       "theirs. Channel values in the same message apply after it.");
    emit_t *forms = emit_array(o, "one_of");
    emit_t *number = emit_object(forms, NULL);
    emit_str(number, "type", "int");
    emit_int(number, "min", 1);
    emit_int(number, "max", PROFILES_MAX);
    emit_t *name = emit_object(forms, NULL);
    emit_str(name, "type", "string");
    emit_int(name, "max_len", PROFILE_NAME_MAX);
}

// Presentation (§7.7): channels labelled with their names, profile numbers with the profile names. The names come
// from /config, so the /definition ETag follows them through config_version.
void profile_emit_ui(emit_t *ui)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    for (int ch = 0; ch < CHANNELS; ch++) {
        if (ch_state[ch].name[0]) {
            emit_str(emit_object(ui, channel_ids[ch]), "label", ch_state[ch].name);
        }
    }
    xSemaphoreGive(lock);
    object_value_t *profiles = params_get_value("profiles");
    emit_t *labels = emit_object(emit_object(ui, "profile"), "value_labels");
    for (int i = 0; i < value_array_size(profiles); i++) {
        const char *name = value_str(value_member(value_array_at(profiles, i), "profile"));
        if (name && *name) {
            char key[12];
            snprintf(key, sizeof(key), "%d", i + 1);
            emit_str(labels, key, name);
        }
    }
    params_value_free(profiles);
    emit_str(emit_object(ui, "profiles"), "doc", "One row per profile; its actions set channels like a control message");
    emit_str(emit_object(ui, "channels"), "doc", "Row n is output channel n");
}

void profile_emit_state(emit_t *profile, bool *busy, const char **fault)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    emit_str(profile, "source", from_master ? "master" : "rest");
    emit_bool(profile, "transmitting", transmitting && !estopped);
    if (last_profile[0]) {
        emit_str(profile, "profile", last_profile);
    } else {
        emit_null(profile, "profile");
    }
    for (int ch = 0; ch < CHANNELS; ch++) {
        const channel_t *c = &ch_state[ch];
        emit_t *o = emit_object(profile, channel_ids[ch]);
        emit_str(o, "name", c->name);
        emit_t *sends = emit_array(o, "sends");
        for (size_t i = 0; i < c->playlist.n; i++) {
            const cm_command_t *cmd = &c->playlist.cmd[i];
            emit_t *e = emit_object(sends, NULL);
            emit_str(e, "cmd", playlist_cmd_name(cmd));
            if (cmd->cmd == CM_CMD_RAW) {
                char hex[2 * CM_IR_MAX_DATA + 1] = "";
                for (size_t k = 0; k < cmd->raw_len; k++) snprintf(hex + 2 * k, 3, "%02X", cmd->raw[k]);
                emit_str(e, "raw", hex);
            } else {
                emit_int(e, "arg", cmd->arg);
            }
            emit_str(e, "range", cmd->short_range ? "short" : "normal");
        }
        if (c->error) {
            emit_str(o, "error", c->error);
        }
    }
    xSemaphoreGive(lock);
    *busy = false;          // a transmitter has nothing a master must wait for
    *fault = NULL;          // No frame counters in state: it must read the same over HTTP and UDP (C-19)
}

static int channel_of(const char *id)
{
    for (int ch = 0; ch < CHANNELS; ch++) {
        if (strcmp(id, channel_ids[ch]) == 0) return ch;
    }
    return -1;
}

// A playlist record -> command. Returns NULL or a reason.
static const char *record_to_command(const object_value_t *r, cm_command_t *c)
{
    const char *name = value_str(value_member(r, "cmd"));
    if (!name) return "missing_cmd";
    if (!playlist_cmd_from_name(name, c)) return "unknown_cmd";
    const object_value_t *arg = value_member(r, "arg");
    long a = 0;
    if (arg && !value_int(arg, &a)) return "wrong_type";
    if (a < 0 || a > 255) return "out_of_range";
    c->arg = (int)a;
    const object_value_t *range = value_member(r, "range");
    if (range) {
        const char *rs = value_str(range);
        if (!rs) return "wrong_type";
        if (strcmp(rs, "short") == 0) c->short_range = true;
        else if (strcmp(rs, "normal") != 0) return "not_allowed";
    }
    const object_value_t *raw = value_member(r, "raw");
    if (raw) {
        const char *hex = value_str(raw);
        if (!hex) return "wrong_type";
        if (*hex) {
            c->raw_len = cm_ir_parse_hex(hex, c->raw, sizeof(c->raw));
            if (!c->raw_len) return "invalid_hex";
        }
    }
    return playlist_check_command(c);
}

static const char *value_to_playlist(const object_value_t *v, playlist_t *p)
{
    int n = value_array_size(v);
    if (n < 0) return "wrong_type";
    if (n > PLAYLIST_MAX) return "too_many_entries";
    p->n = 0;
    for (int i = 0; i < n; i++) {
        const char *reason = record_to_command(value_array_at(v, i), &p->cmd[p->n]);
        if (reason) return reason;
        p->n++;
    }
    return NULL;
}

static const char *value_to_channel(const object_value_t *v, playlist_t *p)
{
    return value_to_playlist(v, p);
}

const char *profile_object_check(const char *id, const object_value_t *value)
{
    if (strcmp(id, "profile") == 0) {
        long n;
        const char *name = value_str(value);
        object_value_t *profiles = params_get_value("profiles");
        const object_value_t *found = NULL;
        const char *reason = NULL;
        if (name) {
            found = find_profile(profiles, name, 0);
        } else if (!value_int(value, &n)) {
            reason = "wrong_type";
        } else if (n < 1 || n > PROFILES_MAX) {
            reason = "out_of_range";
        } else {
            found = find_profile(profiles, NULL, n);
        }
        params_value_free(profiles);
        return reason ? reason : found ? NULL : "unknown_profile";
    }
    if (channel_of(id) < 0) return "unknown_object";
    playlist_t p;
    return value_to_channel(value, &p);
}

void profile_apply(const char *mode, size_t count, const char *const ids[], const object_value_t *const values[])
{
    xSemaphoreTake(lock, portMAX_DELAY);
    // The profile first, then the channels, whatever the order in the message: a channel value overrides the profile
    bool profile_given = false;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(ids[i], "profile") == 0) {
            long n = 0;
            const char *name = value_str(values[i]);
            if (!name) value_int(values[i], &n);
            apply_profile_locked(name, n);
            profile_given = true;
            from_master = true;
        }
    }
    for (size_t i = 0; i < count; i++) {
        int ch = channel_of(ids[i]);
        playlist_t p;
        if (ch >= 0 && !value_to_channel(values[i], &p)) {
            set_playlist_locked(&ch_state[ch], &p, NULL);
            if (!profile_given) {
                last_profile[0] = '\0';         // the channels no longer show a profile as stored
            }
            from_master = true;
        }
    }
    transmitting = (strcmp(mode, "transmit") == 0);
    push_locked();
    xSemaphoreGive(lock);
}

void profile_estop(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    estopped = true;
    push_locked();
    xSemaphoreGive(lock);
}

// Rest state: transmitting, all dark, the start profile applied
void profile_reset(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    estopped = false;
    transmitting = true;
    load_rest_locked();
    push_locked();
    xSemaphoreGive(lock);
}

// Autonomous programme resumes when the master releases or times out (§11.3)
void profile_resume(void)
{
    ESP_LOGI(TAG, "no master: start profile");
    xSemaphoreTake(lock, portMAX_DELAY);
    transmitting = true;
    load_rest_locked();
    push_locked();
    xSemaphoreGive(lock);
}

void profile_come_to_rest(void)
{
    profile_resume();
}

void profile_message(profile_msg_t kind, bool accepted)
{
    (void)kind;
    (void)accepted;
}
