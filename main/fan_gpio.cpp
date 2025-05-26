#include "fan_gpio.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <stdbool.h>

#define LED_GPIO GPIO_NUM_23
#define RELAY_GPIO GPIO_NUM_16

static bool fan_state_cache = false;
static TimerHandle_t fan_off_timer = NULL;

void fan_gpio_led_on(void) {
  gpio_set_level(LED_GPIO, 1); // LED常亮
}

void fan_gpio_led_off(void) {
  gpio_set_level(LED_GPIO, 0); // LED灭
}

void fan_gpio_init(void) {
  gpio_config_t io_conf = {.pin_bit_mask = (1ULL << LED_GPIO) | (1ULL << RELAY_GPIO),
                           .mode = GPIO_MODE_OUTPUT,
                           .pull_up_en = GPIO_PULLUP_DISABLE,
                           .pull_down_en = GPIO_PULLDOWN_DISABLE,
                           .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&io_conf);
  fan_gpio_led_off();            // 默认LED灭
  gpio_set_level(RELAY_GPIO, 0); // 默认风扇关（高电平=开，低电平=关）
  fan_state_cache = false;
}

void blink_led(int times) {
  for (int i = 0; i < times; ++i) {
    int wait_time = 300 / portTICK_PERIOD_MS;
    fan_gpio_led_on();
    vTaskDelay(wait_time);
    fan_gpio_led_off();
    vTaskDelay(wait_time);
  }
}

void change_fan_state(bool on) {
  const char *TAG = "fan_gpio";
  ESP_LOGI(TAG, "change_fan_state: set RELAY_GPIO=%d (high-active)", on ? 1 : 0);
  gpio_set_level(RELAY_GPIO, on ? 1 : 0); // 高电平=开
  fan_state_cache = on;
  int actual = gpio_get_level(RELAY_GPIO);
  ESP_LOGI(TAG, "change_fan_state: actual RELAY_GPIO level=%d", actual);
  blink_led(2);
}

void set_fan_timer(int seconds) {
  if (fan_off_timer) {
    xTimerStop(fan_off_timer, 0);
    xTimerDelete(fan_off_timer, 0);
    fan_off_timer = NULL;
  }
  if (seconds <= 0)
    return;
  fan_off_timer = xTimerCreate("fan_off_timer", pdMS_TO_TICKS(seconds * 1000), pdFALSE, NULL,
                               [](TimerHandle_t xTimer) { change_fan_state(false); });
  if (fan_off_timer) {
    xTimerStart(fan_off_timer, 0);
  }
}

void cancel_fan_timer() {
  if (fan_off_timer) {
    xTimerStop(fan_off_timer, 0);
    xTimerDelete(fan_off_timer, 0);
    fan_off_timer = NULL;
  }
}

bool get_fan_isON(void) { return fan_state_cache; }

bool fan_timer_running() {
  return fan_off_timer != NULL && xTimerIsTimerActive(fan_off_timer) != pdFALSE;
}

int fan_timer_left() {
  if (fan_off_timer && xTimerIsTimerActive(fan_off_timer) != pdFALSE) {
    TickType_t ticks = xTimerGetExpiryTime(fan_off_timer) - xTaskGetTickCount();
    if ((int)ticks < 0)
      return 0;
    return ticks / configTICK_RATE_HZ;
  }
  return 0;
}
