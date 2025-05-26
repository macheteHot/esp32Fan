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
#include "web_server.h"

// 静态HTML页面路径
#define HTML_PATH "/spiffs/html/index.html"

#define BUTTON_GPIO GPIO_NUM_0

static hap_char_t *fan_on_char = NULL;

static const char *TAG = "main";

// HomeKit identify 回调
static int fan_identify(hap_acc_t *ha) {
  ESP_LOGI(TAG, "[HAP] Identify called");
  return HAP_SUCCESS;
}
// HomeKit Fan 服务写回调
static int fan_serv_write(hap_write_data_t *write_data, int count, void *serv_priv,
                          void *write_priv) {
  for (int i = 0; i < count; i++) {
    if (strcmp(hap_char_get_type_uuid(write_data[i].hc), HAP_CHAR_UUID_ON) == 0) {
      change_fan_state(write_data[i].val.b);
    }
  }
  return HAP_SUCCESS;
}

// 恢复出厂回调
void IRAM_ATTR button_factory_reset_cb(void *arg) {
  const char *TAG = "main";
  ESP_LOGW(TAG, "[Button] Long press detected, erasing NVS and restarting...");
  nvs_flash_erase();
  vTaskDelay(10 / portTICK_PERIOD_MS);
  esp_restart();
}

void loop() {
  static bool fan_state = false;
  while (1) {
    // 获取当前风扇状态
    bool current_fan_state = get_fan_isON();
    if (current_fan_state != fan_state) {
      fan_state = current_fan_state;
      // 更新 HomeKit 状态
      hap_val_t fan_on_val = {.b = fan_state};
      hap_char_update_val(fan_on_char, &fan_on_val);
      ESP_LOGI(TAG, "[HAP] Fan state changed: %s", fan_state ? "ON" : "OFF");
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

extern "C" void app_main() {
  // 初始化GPIO（LED/继电器）全部交由fan_gpio_init实现
  fan_gpio_init();
  fan_gpio_led_on(); // 上电即常亮

  // NVS 初始化，确保 WiFi 可用
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }

  // WiFi 配网
  app_wifi_init();
  esp_err_t wifi_ret = app_wifi_start(portMAX_DELAY);

  // WiFi 连接成功后灭掉LED
  if (wifi_ret == ESP_OK) {
    fan_gpio_led_off();
  }

  // HomeKit 配置
  hap_acc_cfg_t cfg = {.name = (char *)"风扇控制器",
                       .model = (char *)"ESP32FAN",
                       .manufacturer = (char *)"虎哥科技",
                       .serial_num = (char *)"20240524",
                       .fw_rev = (char *)"1.0",
                       .hw_rev = NULL,
                       .pv = (char *)"1.1.0",
                       .cid = HAP_CID_FAN,
                       .identify_routine = fan_identify};
  hap_acc_t *acc = hap_acc_create(&cfg);
  hap_acc_add_product_data(acc, (uint8_t *)"ESP32FAN", 8);
  hap_serv_t *fan_serv = hap_serv_fan_create(false);
  fan_on_char = hap_serv_get_char_by_uuid(fan_serv, HAP_CHAR_UUID_ON);
  hap_acc_add_serv(acc, fan_serv);
  hap_add_accessory(acc);
  // 注册 HomeKit Fan 服务写回调
  hap_serv_set_write_cb(fan_serv, fan_serv_write);

  hap_set_setup_code("111-11-111");
  ESP_LOGI(TAG, "HomeKit 配对码: 111-11-111");
  hap_set_setup_id("7G9X");
  hap_init(HAP_TRANSPORT_WIFI);
  hap_start();

  // 按钮逻辑：使用button组件，长按3秒恢复出厂
  static CButton btn(BUTTON_GPIO, BUTTON_ACTIVE_LOW);
  btn.add_on_press_cb(3, button_factory_reset_cb, NULL);

  // LED状态逻辑
  if (wifi_ret != ESP_OK) {
    // 未配网，LED常亮（已在上电时点亮，无需重复）
    while (1) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
    }
  }

  // 启动 HTTP 服务器
  start_http_server();

  loop();
}
