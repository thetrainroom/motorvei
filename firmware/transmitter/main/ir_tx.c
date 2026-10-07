/* SPDX-FileCopyrightText: 2026 Thierry Gschwind
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ir_tx.h"

#include <string.h>
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "ir_tx";

typedef struct {
    rmt_channel_handle_t chan;
    rmt_encoder_handle_t enc;
    cm_code_t list[IR_TX_MAX_CODES];
    size_t n;
    size_t next;
    uint32_t frames;
    rmt_symbol_word_t sym[CM_IR_MAX_PULSES];
    uint16_t long_us;
    uint16_t short_us;
    bool started;
} channel_t;

static channel_t s_ch[IR_TX_MAX_CHANNELS];
static SemaphoreHandle_t s_lock;
static volatile bool s_enabled = true;
static ir_tx_timing_t s_timing = { .gap_min_us = 5000, .gap_max_us = 15000 };

static void channel_task(void *arg)
{
    channel_t *c = arg;
    const rmt_transmit_config_t once = { .loop_count = 0, .flags.eot_level = 0 };
    cm_pulse_t p[CM_IR_MAX_PULSES];
    for (;;) {
        cm_code_t code;
        ir_tx_timing_t t;
        uint16_t long_us, short_us;
        bool have = false;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_enabled && c->n > 0) {
            code = c->list[c->next % c->n];
            c->next = (c->next + 1) % c->n;
            have = true;
        }
        t = s_timing;
        long_us = c->long_us;
        short_us = c->short_us;
        xSemaphoreGive(s_lock);
        if (!have) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        const uint16_t flash = code.long_flash ? long_us : short_us;
        uint32_t gap = t.gap_min_us;
        if (t.gap_max_us > t.gap_min_us) {
            gap += esp_random() % (t.gap_max_us - t.gap_min_us + 1);
        }
        if (gap > 32767) {
            gap = 32767;            // one RMT symbol half holds 15 bits at 1 MHz
        }
        const size_t n = cm_ir_pulses(&code, flash, gap, p);
        for (size_t i = 0; i < n; i++) {
            c->sym[i].level0 = 1;
            c->sym[i].duration0 = p[i].on_us;
            c->sym[i].level1 = 0;
            c->sym[i].duration1 = p[i].off_us;
        }
        ESP_ERROR_CHECK(rmt_transmit(c->chan, c->enc, c->sym, n * sizeof(rmt_symbol_word_t), &once));
        rmt_tx_wait_all_done(c->chan, 100);
        c->frames++;
    }
}

void ir_tx_start(int ch, int gpio)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    channel_t *c = &s_ch[ch];
    c->long_us = 26;
    c->short_us = 21;
    const rmt_tx_channel_config_t cfg = {
        .gpio_num = gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,           // 1 us per tick
        .mem_block_symbols = 48,            // the driver refills for frames longer than one block
        .trans_queue_depth = 2,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&cfg, &c->chan));
    const rmt_copy_encoder_config_t ecfg = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&ecfg, &c->enc));
    ESP_ERROR_CHECK(rmt_enable(c->chan));
    c->started = true;
    char name[12];
    snprintf(name, sizeof(name), "ir_tx%d", ch);
    xTaskCreate(channel_task, name, 4096, c, 5, NULL);
    ESP_LOGI(TAG, "channel %d on GPIO%d", ch + 1, gpio);
}

void ir_tx_set_timing(const ir_tx_timing_t *t)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_timing = *t;
    xSemaphoreGive(s_lock);
}

void ir_tx_set_flash(int ch, uint16_t long_us, uint16_t short_us)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_ch[ch].long_us = long_us;
    s_ch[ch].short_us = short_us;
    xSemaphoreGive(s_lock);
}

void ir_tx_set_playlist(int ch, const cm_code_t *codes, size_t n)
{
    if (n > IR_TX_MAX_CODES) {
        n = IR_TX_MAX_CODES;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(s_ch[ch].list, codes, n * sizeof(cm_code_t));
    s_ch[ch].n = n;
    s_ch[ch].next = 0;
    xSemaphoreGive(s_lock);
}

void ir_tx_set_enabled(bool enabled)
{
    s_enabled = enabled;
}

uint32_t ir_tx_frames(int ch)
{
    return s_ch[ch].frames;
}
