#include "fan_gpio.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
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

static bool fan_is_on_cache = false;
static int fan_level_cache = 1; // 1 低速 2 中速 3 高速
static TimerHandle_t fan_off_timer = NULL;
static QueueHandle_t led_blink_queue = NULL;

// 异步LED闪烁任务
static void led_blink_task(void *arg) {
  int times;
  while (1) {
    if (xQueueReceive(led_blink_queue, &times, portMAX_DELAY) == pdTRUE) {
      const char *TAG = "fan_gpio";
      ESP_LOGI(TAG, "[async] blink_led: start, times=%d", times);
      for (int i = 0; i < times; ++i) {
        int wait_time = 50 / portTICK_PERIOD_MS;
        fan_gpio_led_on();
        vTaskDelay(wait_time);
        fan_gpio_led_off();
        vTaskDelay(wait_time);
      }
      ESP_LOGI(TAG, "[async] blink_led: end");
    }
  }
}

#define FAN_NVS_NAMESPACE "fan_cfg"
#define FAN_NVS_KEY_LEVEL "fan_level"

extern "C" void homekit_fan_state_sync(bool on, int level);

int getFanLevel() { return fan_level_cache; }

static void changeFanLevel(int level) {
  nvs_handle_t handle;
  if (nvs_open(FAN_NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
    nvs_set_i32(handle, FAN_NVS_KEY_LEVEL, level);
    nvs_commit(handle);
    nvs_close(handle);
  }
  fan_level_cache = level; // 更新缓存
}

void fan_gpio_led_on(void) {
  gpio_set_level(LED_GPIO, 1); // LED常亮
}

void fan_gpio_led_off(void) {
  gpio_set_level(LED_GPIO, 0); // LED灭
}

void fan_gpio_init(void) {
  nvs_handle_t handle;
  int32_t level = 1;
  gpio_config_t io_conf = {.pin_bit_mask = (1ULL << LED_GPIO) | (1ULL << HEIGHT_ON) |
                                           (1ULL << MIDDLE_ON) | (1ULL << LOW_ON),
                           .mode = GPIO_MODE_OUTPUT,
                           .pull_up_en = GPIO_PULLUP_DISABLE,
                           .pull_down_en = GPIO_PULLDOWN_DISABLE,
                           .intr_type = GPIO_INTR_DISABLE};
  if (nvs_open(FAN_NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
    nvs_get_i32(handle, FAN_NVS_KEY_LEVEL, &level);
    nvs_close(handle);
  }
  if (level < 1 || level > 3) {
    level = 3;
  }
  gpio_config(&io_conf);
  fan_gpio_led_off();           // 默认LED灭
  gpio_set_level(HEIGHT_ON, 0); // 默认风扇关（高电平=开，低电平=关）
  gpio_set_level(MIDDLE_ON, 0);
  gpio_set_level(LOW_ON, 0);
  fan_is_on_cache = false;
  fan_level_cache = level;

  // 初始化异步LED闪烁队列和任务
  if (!led_blink_queue) {
    led_blink_queue = xQueueCreate(4, sizeof(int));
    xTaskCreate(led_blink_task, "led_blink_task", 2048, NULL, 5, NULL);
  }
}

void blink_led(int times) {
  if (led_blink_queue) {
    xQueueSend(led_blink_queue, &times, 0);
  }
}

void change_fan_state(bool on) { change_fan_state(on, fan_level_cache); }

void change_fan_state(bool on, int level) {
  const char *TAG = "fan_gpio";
  TickType_t t0 = xTaskGetTickCount();
  ESP_LOGI(TAG, "change_fan_state: start, on=%d, level=%d", on, level);
  // level: 1=low, 2=middle, 3=high
  if (level < 1 || level > 3) {
    ESP_LOGE("fan_gpio", "Invalid fan speed level: %d", level);
    return;
  }

  ESP_LOGI(TAG, "change_fan_state: set level=%d (1=low,2=middle,3=high)", level);
  // 关闭所有挡位
  gpio_set_level(HEIGHT_ON, 0);
  gpio_set_level(MIDDLE_ON, 0);
  gpio_set_level(LOW_ON, 0);
  if (on) {
    fan_is_on_cache = true;
    changeFanLevel(level);
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
  } else {
    fan_is_on_cache = false;
  }
  blink_led(2);
  TickType_t t1 = xTaskGetTickCount();
  ESP_LOGI(TAG, "change_fan_state: end, elapsed=%d ms", (int)((t1 - t0) * portTICK_PERIOD_MS));
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
                               [](TimerHandle_t xTimer) {
                                 change_fan_state(false);
                                 homekit_fan_state_sync(false, 0); // 定时关闭后同步HomeKit
                               });
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

bool get_fan_isON(void) { return fan_is_on_cache; }

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
