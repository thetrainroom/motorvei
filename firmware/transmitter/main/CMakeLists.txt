# SPDX-FileCopyrightText: 2026 Thierry Gschwind
# SPDX-License-Identifier: Apache-2.0
idf_component_register(SRCS "main.c" "ir_tx.c" "playlist.c" "profile_carmotion.c"
                       INCLUDE_DIRS "."
                       REQUIRES cm_ir mrroip esp_driver_rmt esp_driver_gpio esp_driver_uart)
