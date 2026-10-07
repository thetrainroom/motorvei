/* SPDX-FileCopyrightText: 2026 Thierry Gschwind
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cm_ir.h"

#include <ctype.h>

uint8_t cm_ir_crc8(const uint8_t *data, size_t len)
{
    // CRC-8/MAXIM (Dallas 1-Wire): poly 0x31 reflected (0x8C), init 0, no final XOR. docs/ir-protocol.md §1
    uint8_t c = 0;
    for (size_t i = 0; i < len; i++) {
        c ^= data[i];
        for (int b = 0; b < 8; b++) {
            c = (c & 1) ? (uint8_t)((c >> 1) ^ 0x8C) : (uint8_t)(c >> 1);
        }
    }
    return c;
}

static size_t make(cm_code_t *out, const uint8_t *cmd, size_t n, bool long_flash)
{
    if (n == 0 || n > CM_IR_MAX_DATA - 1) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        out->data[i] = cmd[i];
    }
    out->data[n] = cm_ir_crc8(cmd, n);
    out->len = n + 1;
    out->long_flash = long_flash;
    return 1;
}

size_t cm_ir_codes(const cm_command_t *c, cm_code_t out[2])
{
    const uint8_t f = c->filter ? (uint8_t)(c->filter & CM_IR_FILTER_ALL) : CM_IR_FILTER_ALL;
    const bool lng = !c->short_range;
    const int a = c->arg;
    uint8_t b[3];

    switch (c->cmd) {
    case CM_CMD_STOP:
    case CM_CMD_CONTINUE: {
        // The stopped-vehicle pair: bit 7 clear with the short flash, bit 7 set with the long flash
        const uint8_t v = c->cmd == CM_CMD_STOP ? 0x01 : 0x00;
        b[0] = f; b[1] = v;
        size_t k = make(&out[0], b, 2, false);
        b[0] = (uint8_t)(f | 0x80);
        k += make(&out[k], b, 2, lng);
        return k;
    }
    case CM_CMD_BRAKE:          b[0] = f | 0x80; b[1] = 0x02; return make(out, b, 2, lng);
    case CM_CMD_RESTORE:        b[0] = f | 0x80; b[1] = 0x03; return make(out, b, 2, lng);
    case CM_CMD_REVERSE_MAGNET: b[0] = f | 0x80; b[1] = 0x14; return make(out, b, 2, lng);
    case CM_CMD_MACRO:
        if (a < 1 || a > 14) return 0;
        b[0] = f | 0x80; b[1] = (uint8_t)(0x03 + a); return make(out, b, 2, lng);
    case CM_CMD_REVERSE:
        if (a < 0 || a > 255) return 0;
        b[0] = f | 0x80; b[1] = 0x13; b[2] = (uint8_t)a;
        return make(out, b, a ? 3 : 2, lng);
    case CM_CMD_LANE:
        if (a < 0 || a > 7) return 0;
        b[0] = f | 0x80; b[1] = (uint8_t)(0x17 + a); return make(out, b, 2, lng);
    case CM_CMD_SPEED:
    case CM_CMD_SPEED_MIN:
    case CM_CMD_SPEED_MAX:
    case CM_CMD_SPEED_DEFAULT: {
        if (a < 0 || a > 155) return 0;
        // Byte 1 bits 7 / 0 select the speed type (docs/ir-protocol.md, Speed)
        static const uint8_t sub[] = { 0x80, 0x00, 0x01, 0x81 };
        b[0] = (uint8_t)(f | sub[c->cmd - CM_CMD_SPEED]);
        b[1] = (uint8_t)(0x20 + a / 5);
        return make(out, b, 2, lng);
    }
    case CM_CMD_FUNCTION_ON:
    case CM_CMD_FUNCTION_OFF:
        if (a < 0 || a > (c->permanent ? 7 : 11)) return 0;
        b[0] = (uint8_t)(c->cmd == CM_CMD_FUNCTION_ON ? (f | 0x80) : f);
        b[1] = (uint8_t)(0x60 + a + (c->permanent ? 0x10 : 0));
        return make(out, b, 2, lng);
    case CM_CMD_MAGNET:
        if (a < 0 || a > 7) return 0;
        b[0] = f | 0x80; b[1] = (uint8_t)(0x40 + a); return make(out, b, 2, lng);
    case CM_CMD_ROLLING_HIGHWAY:
        if (a < 0 || a > 2) return 0;
        b[0] = f | 0x80; b[1] = (uint8_t)(0x50 + a); b[2] = 0xF0; return make(out, b, 3, lng);
    case CM_CMD_RAW:
        return make(out, c->raw, c->raw_len, lng);
    default:
        return 0;
    }
}

static bool flag_of(const cm_code_t *code)
{
    // Send the polarity with fewer 1-bits (shorter frame); a tie sends inverted (flag 0). docs/ir-protocol.md §1
    int ones = 0;
    for (size_t i = 0; i < code->len; i++) {
        ones += __builtin_popcount(code->data[i]);
    }
    return ones < (int)(8 * code->len) - ones;
}

size_t cm_ir_bits(const cm_code_t *code, uint8_t bits[1 + 8 * CM_IR_MAX_DATA])
{
    const bool flag = flag_of(code);
    size_t n = 0;
    bits[n++] = flag;
    for (size_t i = 0; i < code->len; i++) {
        const uint8_t air = flag ? code->data[i] : (uint8_t)~code->data[i];
        for (int b = 7; b >= 0; b--) {
            bits[n++] = (air >> b) & 1;
        }
    }
    return n;
}

size_t cm_ir_block(const cm_code_t *code, bool separate_channels, uint8_t block[1 + CM_IR_MAX_DATA])
{
    // Header: bit 7 flag, bit 6 long flash, bit 5 separate channels, bits 0-2 length. Stored bytes are the
    // on-air bytes inverted. docs/ir-protocol.md §4
    const bool flag = flag_of(code);
    block[0] = (uint8_t)((flag ? 0x80 : 0) | (code->long_flash ? 0x40 : 0) | (separate_channels ? 0x20 : 0) |
                         (code->len & 0x07));
    for (size_t i = 0; i < code->len; i++) {
        const uint8_t air = flag ? code->data[i] : (uint8_t)~code->data[i];
        block[1 + i] = (uint8_t)~air;
    }
    return 1 + code->len;
}

size_t cm_ir_pulses(const cm_code_t *code, uint16_t flash_us, uint32_t gap_us, cm_pulse_t out[CM_IR_MAX_PULSES])
{
    uint8_t bits[1 + 8 * CM_IR_MAX_DATA];
    const size_t nbits = cm_ir_bits(code, bits);
    uint16_t periods[3 + 1 + 8 * CM_IR_MAX_DATA + 1];
    size_t np = 0;
    for (int i = 0; i < 3; i++) periods[np++] = CM_IR_PERIOD_S_US;     // sync
    for (size_t i = 0; i < nbits; i++) periods[np++] = bits[i] ? CM_IR_PERIOD_1_US : CM_IR_PERIOD_0_US;
    periods[np++] = CM_IR_PERIOD_S_US;                                   // end
    size_t n = 0;
    for (size_t i = 0; i < np; i++) {
        out[n].on_us = flash_us;
        out[n].off_us = (uint32_t)(periods[i] - flash_us);
        n++;
    }
    out[n].on_us = flash_us;        // the flash that closes the end symbol
    out[n].off_us = gap_us;
    return n + 1;
}

static int hexval(int ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    ch = tolower(ch);
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

size_t cm_ir_parse_hex(const char *s, uint8_t *out, size_t max)
{
    size_t n = 0;
    int hi = -1;
    for (; *s; s++) {
        if (isspace((unsigned char)*s)) continue;
        const int v = hexval((unsigned char)*s);
        if (v < 0) return 0;
        if (hi < 0) {
            hi = v;
        } else {
            if (n >= max) return 0;
            out[n++] = (uint8_t)(hi << 4 | v);
            hi = -1;
        }
    }
    return hi < 0 ? n : 0;
}
