#include "app_wifi.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_button.h"
#include <fstream>
#include <nvs_flash.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
extern "C" {
#include "hap.h"
#include "hap_apple_chars.h"
#include "hap_apple_servs.h"
#include "sdkconfig.h"
#include "wifi_provisioning/manager.h"
}
#include "esp_log.h"
#include "fan_gpio.h" // 新增，所有GPIO操作通过此接口
#include "homekit.h"
#include "web_server.h"

// 静态HTML页面路径
#define HTML_PATH "/spiffs/html/index.html"

#define BUTTON_GPIO GPIO_NUM_0

static const char *TAG = "main";

// 恢复出厂回调
void IRAM_ATTR button_factory_reset_cb(void *arg) {
  ESP_LOGW(TAG, "[Button] Long press detected, erasing NVS and restarting...");
  nvs_flash_erase();
  vTaskDelay(10 / portTICK_PERIOD_MS);
  esp_restart();
}

extern "C" void app_main() {
  // 初始化GPIO（LED/继电器）全部交由fan_gpio_init实现
  fan_gpio_init();
  fan_gpio_led_on(); // 上电即常亮

  // NVS 初始化，确保 WiFi 可用
  esp_err_t ret = nvs_flash_init();
  // 不再注册fan_gpio回调
  // register_fan_state_callback(homekit_fan_state_cb); // 删除

  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }

  // WiFi 配网
  app_wifi_init();
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (netif) {
    esp_netif_set_hostname(netif, "风扇");
  }
  esp_err_t wifi_ret = app_wifi_start(portMAX_DELAY);

  // WiFi 连接成功后灭掉LED
  if (wifi_ret == ESP_OK) {
    fan_gpio_led_off();
  }

  // HomeKit 初始化
  homekit_init();

  // 按钮逻辑：使用button组件，长按3秒恢复出厂
  static CButton btn(BUTTON_GPIO, BUTTON_ACTIVE_LOW);
  btn.add_on_press_cb(3, button_factory_reset_cb, NULL);

  // 未配网，LED常亮（已在上电时点亮，无需重复）
  while (wifi_ret != ESP_OK) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }

  // 启动 HTTP 服务器
  start_http_server();

  while (1) { // 主循环，保持任务运行
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}
