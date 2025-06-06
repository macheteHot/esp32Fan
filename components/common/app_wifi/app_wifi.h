/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#pragma once
#include "freertos/FreeRTOS.h"
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_wifi_init(void);
esp_err_t app_wifi_start(TickType_t ticks_to_wait);

#ifdef __cplusplus
}
#endif
