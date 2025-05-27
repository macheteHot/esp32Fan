#include "fan_gpio.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdbool.h>

#define LED_GPIO GPIO_NUM_23
#define HEIGHT_ON GPIO_NUM_16
#define MIDDLE_ON GPIO_NUM_33
#define LOW_ON GPIO_NUM_32
// GPIO定义：LED常亮，风扇开关（高电平=开，低电平=关）
// 注意：GPIO_NUM_16, GPIO_NUM_33, GPIO_NUM_32 需要根据实际硬件连接调整

static bool fan_state_cache = false;
static TimerHandle_t fan_off_timer = NULL;

#define FAN_NVS_NAMESPACE "fan_cfg"
#define FAN_NVS_KEY_LEVEL "fan_level"

int get_last_fan_level() {
  nvs_handle_t handle;
  int32_t level = 3;
  if (nvs_open(FAN_NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
    nvs_get_i32(handle, FAN_NVS_KEY_LEVEL, &level);
    nvs_close(handle);
  }
  if (level < 1 || level > 3)
    level = 3;
  return level;
}

static void save_fan_level_to_nvs(int level) {
  nvs_handle_t handle;
  if (nvs_open(FAN_NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
    nvs_set_i32(handle, FAN_NVS_KEY_LEVEL, level);
    nvs_commit(handle);
    nvs_close(handle);
  }
}

void fan_gpio_led_on(void) {
  gpio_set_level(LED_GPIO, 1); // LED常亮
}

void fan_gpio_led_off(void) {
  gpio_set_level(LED_GPIO, 0); // LED灭
}

void fan_gpio_init(void) {
  gpio_config_t io_conf = {.pin_bit_mask = (1ULL << LED_GPIO) | (1ULL << HEIGHT_ON) |
                                           (1ULL << MIDDLE_ON) | (1ULL << LOW_ON),
                           .mode = GPIO_MODE_OUTPUT,
                           .pull_up_en = GPIO_PULLUP_DISABLE,
                           .pull_down_en = GPIO_PULLDOWN_DISABLE,
                           .intr_type = GPIO_INTR_DISABLE};

  gpio_config(&io_conf);
  fan_gpio_led_off();           // 默认LED灭
  gpio_set_level(HEIGHT_ON, 0); // 默认风扇关（高电平=开，低电平=关）
  gpio_set_level(MIDDLE_ON, 0);
  gpio_set_level(LOW_ON, 0);
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

void change_fan_state(bool on) { change_fan_state(on, get_last_fan_level()); }

void change_fan_state(bool on, int level) {
  // level: 0=off, 1=low, 2=middle, 3=high
  if (level < 0 || level > 3) {
    ESP_LOGE("fan_gpio", "Invalid fan speed level: %d", level);
    return;
  }
  const char *TAG = "fan_gpio";
  ESP_LOGI(TAG, "change_fan_state: set level=%d (0=off,1=low,2=middle,3=high)", level);
  // 关闭所有挡位
  gpio_set_level(HEIGHT_ON, 0);
  gpio_set_level(MIDDLE_ON, 0);
  gpio_set_level(LOW_ON, 0);
  if (on) {
    switch (level) {
    case 1:
      gpio_set_level(LOW_ON, 1);
      break;
    case 2:
      gpio_set_level(MIDDLE_ON, 1);
      break;
    case 3:
    default:
      gpio_set_level(HEIGHT_ON, 1);
      break;
    }
    fan_state_cache = true;
    save_fan_level_to_nvs(level);
  } else {
    fan_state_cache = false;
  }
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
