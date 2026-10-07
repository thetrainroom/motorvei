/* SPDX-FileCopyrightText: 2026 Thierry Gschwind
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * IR output channels: each channel repeats its playlist of CarMotion codes round-robin, one RMT channel per
 * output, flashes active high (GPIO -> MOSFET gate -> IR LED). docs/module-design.md "Transmitter".
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cm_ir.h"

#define IR_TX_MAX_CHANNELS  2       // ESP32-C3: 2 RMT TX channels
#define IR_TX_MAX_CODES     16      // codes per playlist (a command expands to 1 or 2 codes)

typedef struct {
    uint32_t gap_min_us;            // darkness after a frame, randomised between min and max
    uint32_t gap_max_us;
} ir_tx_timing_t;

// Start channel ch on gpio. Channels start with an empty playlist (dark).
void ir_tx_start(int ch, int gpio);
void ir_tx_set_timing(const ir_tx_timing_t *t);
// Flash widths of one channel. Original IR Mini: transmitter power 25 -> 26 us long, 25 - 5 -> 21 us short.
void ir_tx_set_flash(int ch, uint16_t long_us, uint16_t short_us);
// Replace a channel's playlist (whole list, applied at the next frame boundary). n = 0 makes it dark.
void ir_tx_set_playlist(int ch, const cm_code_t *codes, size_t n);
// false: all channels dark after the current frame (off mode, estop)
void ir_tx_set_enabled(bool enabled);
uint32_t ir_tx_frames(int ch);
