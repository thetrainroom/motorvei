/* SPDX-FileCopyrightText: 2026 Thierry Gschwind
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * CarMotion IR transmitter as an MRRoIP endpoint (profile carmotion, profile_carmotion.c) on the Ai-Thinker
 * ESP-C3-12F-Kit. Channel 1 on IO10, channel 2 on IO1; a flash is GPIO high.
 *
 * Boot order as in ../../../mrroip: mrroip_init (flash store, parameters), console, profile, mrroip_start.
 * Serial console (115200, UART0 via the kit's CH340):
 *     wifi               network state, setup network name and password (no display on this board)
 *     wifi forget        erase the stored Wi-Fi credentials and restart into setup mode
 *     frames             frames sent per channel
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ir_tx.h"
#include "mrroip.h"
#include "mrroip_profile.h"

#define CONSOLE_UART        UART_NUM_0
#define CONSOLE_LINE_MAX    128

static void handle_command(char *line)
{
    char *cmd = strtok(line, " \t");
    char *arg = strtok(NULL, " \t");
    if (!cmd) {
        return;
    }
    if (strcmp(cmd, "frames") == 0) {
        printf("ch1 %lu, ch2 %lu\n", (unsigned long)ir_tx_frames(0), (unsigned long)ir_tx_frames(1));
    } else if (!mrroip_console_command(cmd, arg)) {
        printf("commands: frames | " MRROIP_CONSOLE_HELP "\n");
    }
}

static void console_task(void *arg)
{
    (void)arg;
    char line[CONSOLE_LINE_MAX];
    size_t len = 0;
    for (;;) {
        uint8_t c;
        if (uart_read_bytes(CONSOLE_UART, &c, 1, portMAX_DELAY) != 1) {
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (len > 0) {
                printf("\n");
                line[len] = '\0';
                handle_command(line);
            }
            len = 0;
        } else if ((c == 0x08 || c == 0x7F) && len > 0) {
            len--;
        } else if (isprint(c) && len < sizeof(line) - 1) {
            line[len++] = (char)c;
            putchar(c);
            fflush(stdout);
        }
    }
}

void app_main(void)
{
    mrroip_init();
    ESP_ERROR_CHECK(uart_driver_install(CONSOLE_UART, 1024, 0, 0, NULL, 0));
    xTaskCreate(console_task, "console", 4 * 1024, NULL, 1, NULL);
    profile_start();
    mrroip_start(&(mrroip_config_t){ .ethernet = NULL });
    printf("carmotion: commands: frames | " MRROIP_CONSOLE_HELP "\n");
}
