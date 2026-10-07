/* SPDX-FileCopyrightText: 2026 Thierry Gschwind
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Host test for components/cm_ir: every expected block below was written by CarManager 1.32 to an IR Mini 1.06
 * and captured over USB (captures/usb/, see docs/ir-protocol.md and captures/sweep.csv).
 *
 *   cc -Wall -Wextra -I components/cm_ir/include components/cm_ir/cm_ir.c test/test_cm_ir.c -o /tmp/test_cm_ir
 *   /tmp/test_cm_ir
 */
#include <stdio.h>
#include <string.h>

#include "cm_ir.h"

static int failures, checks;

static void hex(const uint8_t *b, size_t n, char *out)
{
    out[0] = 0;
    for (size_t i = 0; i < n; i++) {
        sprintf(out + strlen(out), i ? " %02X" : "%02X", b[i]);
    }
}

// Expect the command's codes to produce these CarManager blocks (one or two, space separated hex)
static void expect(const char *what, cm_command_t c, bool separate, const char *block1, const char *block2)
{
    cm_code_t codes[2];
    const size_t n = cm_ir_codes(&c, codes);
    const char *want[2] = { block1, block2 };
    const size_t wn = block2 ? 2 : 1;
    checks++;
    if (n != wn) {
        printf("FAIL %-40s %zu codes, want %zu\n", what, n, wn);
        failures++;
        return;
    }
    for (size_t i = 0; i < n; i++) {
        uint8_t blk[1 + CM_IR_MAX_DATA];
        char got[64];
        hex(blk, cm_ir_block(&codes[i], separate, blk), got);
        if (strcmp(got, want[i]) != 0) {
            printf("FAIL %-40s code %zu: %s, want %s\n", what, i + 1, got, want[i]);
            failures++;
            return;
        }
    }
    printf("ok   %-40s %s%s%s\n", what, block1, block2 ? " | " : "", block2 ? block2 : "");
}

static cm_command_t cmd(cm_cmd_t k, int arg)
{
    cm_command_t c = { .cmd = k, .arg = arg };
    return c;
}

int main(void)
{
    // Basic commands (usb/05, 16, 18, 98-103)
    expect("stop", cmd(CM_CMD_STOP, 0), false, "03 3E 01 AF", "C3 41 FE 7F");
    expect("continue / drive away", cmd(CM_CMD_CONTINUE, 0), false, "83 C1 FF 0E", "43 BE 00 DE");
    expect("brake", cmd(CM_CMD_BRAKE, 0), false, "C3 41 FD 9D", NULL);
    expect("restore default driving mode", cmd(CM_CMD_RESTORE, 0), false, "43 BE 03 3C", NULL);
    expect("reverse in range", cmd(CM_CMD_REVERSE, 0), false, "43 BE 13 A1", NULL);
    expect("reverse in range, 30 cm", cmd(CM_CMD_REVERSE, 30), false, "44 BE 13 1E 73", NULL);
    expect("reverse until magnet", cmd(CM_CMD_REVERSE_MAGNET, 0), false, "C3 41 EB DD", NULL);

    // Macros (usb/106-119)
    expect("macro 1", cmd(CM_CMD_MACRO, 1), false, "43 BE 04 BF", NULL);
    expect("macro 3", cmd(CM_CMD_MACRO, 3), false, "C3 41 F9 FC", NULL);
    expect("macro 14", cmd(CM_CMD_MACRO, 14), false, "43 BE 11 1D", NULL);

    // Traffic lanes (usb/15)
    expect("lane 0 (stopping lane)", cmd(CM_CMD_LANE, 0), false, "43 BE 17 C0", NULL);
    expect("traffic direction B", cmd(CM_CMD_LANE, 7), false, "43 BE 1E 5C", NULL);

    // Speed (usb/10, 11, 130-133)
    expect("speed 0 km/h", cmd(CM_CMD_SPEED, 0), false, "43 BE 20 FD", NULL);
    expect("speed 10 km/h", cmd(CM_CMD_SPEED, 10), false, "C3 41 DD BE", NULL);
    expect("speed 50 km/h", cmd(CM_CMD_SPEED, 50), false, "43 BE 2A 83", NULL);
    expect("speed 110 km/h", cmd(CM_CMD_SPEED, 110), false, "43 BE 36 BD", NULL);
    expect("minimum speed 50", cmd(CM_CMD_SPEED_MIN, 50), false, "43 3E 2A AC", NULL);
    expect("maximum speed 50", cmd(CM_CMD_SPEED_MAX, 50), false, "43 3F 2A 68", NULL);
    expect("default speed 50", cmd(CM_CMD_SPEED_DEFAULT, 50), false, "43 BF 2A 47", NULL);

    // Functions (usb/12-14)
    cm_command_t f = cmd(CM_CMD_FUNCTION_ON, 0);
    f.permanent = true;
    expect("main lights on, permanent", f, false, "43 BE 70 26", NULL);
    f.cmd = CM_CMD_FUNCTION_OFF;
    expect("main lights off, permanent", f, false, "C3 C1 8F F6", NULL);
    expect("main lights off", cmd(CM_CMD_FUNCTION_OFF, 0), false, "C3 C1 9F 6B", NULL);
    expect("high beam off", cmd(CM_CMD_FUNCTION_OFF, 1), false, "43 3E 61 CA", NULL);
    expect("sound: shout off", cmd(CM_CMD_FUNCTION_OFF, 11), false, "43 3E 6B B4", NULL);

    // Magnetic sequence (usb/17)
    expect("magnet N", cmd(CM_CMD_MAGNET, 0), false, "C3 41 BF 67", NULL);   // usb/09, usb/105
    expect("magnet SSS", cmd(CM_CMD_MAGNET, 7), false, "43 BE 47 1B", NULL);

    // Rolling highway (usb/124-126)
    expect("rolling highway exit", cmd(CM_CMD_ROLLING_HIGHWAY, 0), false, "44 BE 50 F0 4B", NULL);
    expect("rolling highway enter", cmd(CM_CMD_ROLLING_HIGHWAY, 1), false, "44 BE 51 F0 8F", NULL);
    expect("rolling highway enter, packed", cmd(CM_CMD_ROLLING_HIGHWAY, 2), false, "44 BE 52 F0 DA", NULL);

    // Recipient filter: lane 0 off (usb/37)
    cm_command_t s = cmd(CM_CMD_STOP, 0);
    s.filter = 0x36;
    expect("stop, filter lane 0 off", s, false, "83 C9 FE 26", "43 B6 01 F6");

    // Separate channels (usb/16) and the self-built dual-channel blocks (usb/21, 23, 24)
    cm_command_t r = { .cmd = CM_CMD_RAW, .raw = { 0x3E, 0x40 }, .raw_len = 2, .short_range = true };
    expect("stop at magnet (brake+stop code 1)", r, true, "23 3E 40 B7", NULL);
    expect("brake (brake+stop code 2)", cmd(CM_CMD_BRAKE, 0), true, "E3 41 FD 9D", NULL);
    cm_command_t v = cmd(CM_CMD_SPEED, 30);
    v.short_range = true;
    expect("self-built: speed 30, separate, short", v, true, "A3 41 D9 DF", NULL);
    f = cmd(CM_CMD_FUNCTION_ON, 0);
    f.permanent = true;
    f.short_range = true;
    expect("self-built: lights on perm, separate", f, true, "23 BE 70 26", NULL);

    // On-air bits and pulse train of the stop code 2 frame (scope 2026-09-26_01, frame A)
    cm_code_t codes[2];
    cm_command_t st = cmd(CM_CMD_STOP, 0);
    cm_ir_codes(&st, codes);
    uint8_t bits[1 + 8 * CM_IR_MAX_DATA];
    char bs[64] = { 0 };
    const size_t nb = cm_ir_bits(&codes[1], bits);
    for (size_t i = 0; i < nb; i++) bs[i] = (char)('0' + bits[i]);
    checks++;
    if (strcmp(bs, "1101111100000000110000000") != 0) {
        printf("FAIL on-air bits %s\n", bs);
        failures++;
    } else {
        printf("ok   on-air bits stop code 2              %s\n", bs);
    }
    cm_pulse_t p[CM_IR_MAX_PULSES];
    const size_t np = cm_ir_pulses(&codes[1], 26, 5000, p);
    checks++;
    if (np != 30 || p[0].on_us != 26 || p[0].off_us != 169 - 26 || p[3].off_us != 130 - 26 || p[29].off_us != 5000) {
        printf("FAIL pulses: %zu\n", np);
        failures++;
    } else {
        printf("ok   pulse train                          30 flashes, first period 169 us\n");
    }

    // Hex parsing
    uint8_t raw[4];
    checks++;
    if (cm_ir_parse_hex("be 2a", raw, 4) != 2 || raw[0] != 0xBE || raw[1] != 0x2A || cm_ir_parse_hex("b", raw, 4) != 0) {
        printf("FAIL parse_hex\n");
        failures++;
    } else {
        printf("ok   parse_hex\n");
    }

    printf("\n%d of %d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
