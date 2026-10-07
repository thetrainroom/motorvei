/* SPDX-FileCopyrightText: 2026 Thierry Gschwind
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Viessmann CarMotion native IR protocol (module -> vehicle): commands, frames, pulse timing.
 * Protocol reference: docs/ir-protocol.md, docs/physical-layer.md. Plain C, no ESP-IDF dependency, so the host
 * tests (test/test_cm_ir.c) run the same code as the firmware.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CM_IR_MAX_DATA      7       // command bytes + CRC; the header length field allows 7
#define CM_IR_MAX_PULSES    (4 + 1 + 8 * CM_IR_MAX_DATA + 1)

// Symbol periods, flash start to flash start (us). docs/physical-layer.md
#define CM_IR_PERIOD_0_US   90
#define CM_IR_PERIOD_1_US   130
#define CM_IR_PERIOD_S_US   169

// Byte 1 recipient filter (docs/ir-protocol.md §2): bits 1-5, all set = every lane and direction
#define CM_IR_FILTER_ALL    0x3E

typedef enum {
    CM_CMD_STOP,            // two codes: 3E 01 (short flash) + BE 01 (long flash), like a stopped vehicle
    CM_CMD_CONTINUE,        // two codes: 3E 00 + BE 00
    CM_CMD_BRAKE,           // BE 02
    CM_CMD_RESTORE,         // BE 03 restore default driving mode
    CM_CMD_MACRO,           // BE 03+n, arg n = 1..14
    CM_CMD_REVERSE,         // BE 13 [cm], arg = custom distance in cm (0 = none)
    CM_CMD_REVERSE_MAGNET,  // BE 14
    CM_CMD_LANE,            // BE 17+arg, arg 0..7: lane 0/1/2, special on/off/toggle, direction A/B
    CM_CMD_SPEED,           // BE 20+arg/5 (set speed to value), arg km/h 0..155
    CM_CMD_SPEED_MIN,       // 3E ..
    CM_CMD_SPEED_MAX,       // 3F ..
    CM_CMD_SPEED_DEFAULT,   // BF ..
    CM_CMD_FUNCTION_ON,     // BE 60+arg (+10 permanent), arg function 0..11
    CM_CMD_FUNCTION_OFF,    // 3E 60+arg (+10 permanent)
    CM_CMD_MAGNET,          // BE 40+arg, arg = pole pattern 0..7 (N, S, SN, SS, SNN, SNS, SSN, SSS)
    CM_CMD_ROLLING_HIGHWAY, // BE 50+arg F0, arg 0 exit, 1 enter, 2 enter tightly packed
    CM_CMD_RAW,             // command bytes given by the caller (without CRC)
    CM_CMD_COUNT
} cm_cmd_t;

typedef struct {
    cm_cmd_t cmd;
    int arg;
    bool permanent;         // functions only
    bool short_range;       // use the short flash for every code of this command
    uint8_t filter;         // recipient filter bits 1-5 of byte 1; 0 means CM_IR_FILTER_ALL
    uint8_t raw[CM_IR_MAX_DATA - 1];
    size_t raw_len;
} cm_command_t;

// One transmitted frame: data bytes including CRC, and whether it uses the long flash
typedef struct {
    uint8_t data[CM_IR_MAX_DATA];
    size_t len;
    bool long_flash;
} cm_code_t;

// One IR pulse: flash of width `on_us`, then dark for `off_us` until the next flash (or the inter-frame gap)
typedef struct {
    uint16_t on_us;
    uint32_t off_us;
} cm_pulse_t;

uint8_t cm_ir_crc8(const uint8_t *data, size_t len);

// Command -> the codes it transmits (1 or 2). Returns the number of codes, 0 if the command is invalid.
size_t cm_ir_codes(const cm_command_t *c, cm_code_t out[2]);

// Code -> on-air bit string: flag bit then the (possibly inverted) bytes. Returns the number of bits.
size_t cm_ir_bits(const cm_code_t *code, uint8_t bits[1 + 8 * CM_IR_MAX_DATA]);

// Code -> the CarManager/IR Mini code block (header + stored bytes), as written to the module's CVs
size_t cm_ir_block(const cm_code_t *code, bool separate_channels, uint8_t block[1 + CM_IR_MAX_DATA]);

// Code -> pulses for one frame. flash_us is the flash width; gap_us the darkness after the last flash.
// Returns the number of pulses.
size_t cm_ir_pulses(const cm_code_t *code, uint16_t flash_us, uint32_t gap_us, cm_pulse_t out[CM_IR_MAX_PULSES]);

// Parse "BE 2A" / "be2a" hex into raw bytes. Returns the count, or 0 on a syntax error.
size_t cm_ir_parse_hex(const char *s, uint8_t *out, size_t max);
