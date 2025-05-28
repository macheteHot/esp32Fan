#include "esp_log.h"
#include "fan_gpio.h"
#include <cstring>
extern "C" {
#include "hap.h"
#include "hap_apple_chars.h"
#include "hap_apple_servs.h"
}

static hap_char_t *fan_speed_char = NULL;
static hap_char_t *fan_on_char = NULL;
static const char *TAG = "homekit";

// HomeKit Fan 状态同步函数
extern "C" void homekit_fan_state_sync(bool on, int level) {
  float speed = 0.0f;
  if (!on) {
    speed = 0.0f;
  } else if (level == 1) {
    speed = 33.0f;
  } else if (level == 2) {
    speed = 66.0f;
  } else if (level == 3) {
    speed = 100.0f;
  }
  if (fan_speed_char) {
    hap_val_t val;
    val.f = speed;
    hap_char_update_val(fan_speed_char, &val);
  }
  if (fan_on_char) {
    hap_val_t val;
    val.b = on;
    hap_char_update_val(fan_on_char, &val);
  }

  ESP_LOGI(TAG, "[HAP] Fan state sync: on=%d, level=%d, speed=%.2f%%", on, level, speed);
}

static void fan_hap_event_handler(void *arg, esp_event_base_t event_base, int32_t event,
                                  void *data) {
  switch (event) {
  case HAP_EVENT_PAIRING_STARTED:
    ESP_LOGI(TAG, "Pairing Started");
    break;
  case HAP_EVENT_PAIRING_ABORTED:
    ESP_LOGI(TAG, "Pairing Aborted");
    break;
  case HAP_EVENT_CTRL_PAIRED:
    ESP_LOGI(TAG, "Controller %s Paired. Controller count: %d", (char *)data,
             hap_get_paired_controller_count());
    break;
  case HAP_EVENT_CTRL_UNPAIRED:
    ESP_LOGI(TAG, "Controller %s Removed. Controller count: %d", (char *)data,
             hap_get_paired_controller_count());
    break;
  case HAP_EVENT_CTRL_CONNECTED:
    ESP_LOGI(TAG, "Controller %s Connected", (char *)data);
    break;
  case HAP_EVENT_CTRL_DISCONNECTED:
    ESP_LOGI(TAG, "Controller %s Disconnected", (char *)data);
    break;
  case HAP_EVENT_ACC_REBOOTING: {
    char *reason = (char *)data;
    ESP_LOGI(TAG, "Accessory Rebooting (Reason: %s)", reason ? reason : "null");
    break;
  }
  case HAP_EVENT_PAIRING_MODE_TIMED_OUT:
    ESP_LOGI(TAG, "Pairing Mode timed out. Please reboot the device.");
    break;
  default:
    /* Silently ignore unknown events */
    break;
  }
}

// HomeKit identify 回调
static int fan_identify(hap_acc_t *ha) {
  ESP_LOGI(TAG, "[HAP] Identify called");
  return HAP_SUCCESS;
}

// HomeKit Fan 服务写回调
static int fan_serv_write(hap_write_data_t *write_data, int count, void *serv_priv,
                          void *write_priv) {
  bool on_updated = false;
  bool on = get_fan_isON();
  float speed = 100.0f;
  for (int i = 0; i < count; i++) {
    ESP_LOGI(TAG, "[HAP] Write data[%d]: hc=%p, val.b=%d, val.f=%.2f, remote=%d", i,
             write_data[i].hc, write_data[i].val.b, write_data[i].val.f, write_data[i].remote);
    const char *uuid = hap_char_get_type_uuid(write_data[i].hc);
    if (strcmp(uuid, HAP_CHAR_UUID_ON) == 0) {
      on = write_data[i].val.b;
      on_updated = true;
    } else if (strcmp(uuid, HAP_CHAR_UUID_ROTATION_SPEED) == 0) {
      speed = write_data[i].val.f;
    }
  }
  ESP_LOGI(TAG, "[HAP] Fan write: on=%d, speed=%.2f%%", on, speed);
  int level = 3;
  if (on_updated && !on) {
    change_fan_state(false);
    homekit_fan_state_sync(false, level); // 正确：同步关机
    return HAP_SUCCESS;
  }
  if (speed == 0) {
    change_fan_state(false);
    homekit_fan_state_sync(false, level); // 正确：同步关机
    return HAP_SUCCESS;
  }
  if (speed < 34)
    level = 1;
  else if (speed < 67)
    level = 2;
  else
    level = 3;
  change_fan_state(true, level);
  homekit_fan_state_sync(true, level); // 同步到 HomeKit
  return HAP_SUCCESS;
}

extern "C" void homekit_init() {
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
  hap_acc_add_wifi_transport_service(acc, 0);

  // 使用 v1 Fan服务（只带 On），手动添加风速特征
  hap_serv_t *fan_serv = hap_serv_fan_create(false);
  hap_char_t *rotation_speed = hap_char_rotation_speed_create(100.0f);
  hap_serv_add_char(fan_serv, rotation_speed);
  char fan_name[] = "风扇";
  hap_serv_add_char(fan_serv, hap_char_name_create(fan_name));
  // 获取特征指针
  fan_speed_char = rotation_speed;
  fan_on_char = hap_serv_get_char_by_uuid(fan_serv, HAP_CHAR_UUID_ON);
  if (fan_speed_char) {
    hap_char_float_set_constraints(fan_speed_char, 0.0f, 100.0f, 33.0f); // 三挡
  }

  // 注册写回调
  hap_serv_set_write_cb(fan_serv, fan_serv_write);
  hap_acc_add_serv(acc, fan_serv);
  hap_add_accessory(acc);
  hap_set_setup_code("111-11-111");
  ESP_LOGI(TAG, "HomeKit 配对码: 111-11-111");
  hap_set_setup_id("7G9X");
  hap_init(HAP_TRANSPORT_WIFI);
  esp_event_handler_register(HAP_EVENT, ESP_EVENT_ANY_ID, &fan_hap_event_handler, NULL);
  hap_start();
}
