#include "driver/gpio.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "fan_gpio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

// CORS 统一处理
static esp_err_t cors_preflight_handler(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

// 包装API响应，自动加CORS头
static void set_cors_headers(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
}

static const char *TAG = "web_server";
// /api/on 处理函数
static esp_err_t api_on_handler(httpd_req_t *req) {
  set_cors_headers(req);
  change_fan_state(true);
  httpd_resp_sendstr(req, "{\"result\":true,\"status\":1}");
  return ESP_OK;
}
// /api/off 处理函数
static esp_err_t api_off_handler(httpd_req_t *req) {
  set_cors_headers(req);
  change_fan_state(false);
  httpd_resp_sendstr(req, "{\"result\":true,\"status\":0}");
  return ESP_OK;
}

extern bool fan_timer_running();
extern int fan_timer_left();

static esp_err_t api_timer_off_handler(httpd_req_t *req) {
  set_cors_headers(req);
  char query[64] = {0};
  int timer_sec = 0;
  // 获取 GET 参数 seconds
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char seconds_str[16] = {0};
    if (httpd_query_key_value(query, "seconds", seconds_str, sizeof(seconds_str)) == ESP_OK) {
      timer_sec = atoi(seconds_str);
    }
  }
  if (timer_sec <= 0) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"result\":false,\"error\":\"invalid timer\"}");
    return ESP_OK;
  }
  // 新的请求直接取消老的定时器并设置新的
  cancel_fan_timer();
  set_fan_timer(timer_sec);
  char resp[64];
  snprintf(resp, sizeof(resp), "{\"result\":true,\"timer\":%d}", timer_sec);
  httpd_resp_sendstr(req, resp);
  return ESP_OK;
}

// /api/cancel_timer 处理函数
static esp_err_t api_cancel_timer_handler(httpd_req_t *req) {
  set_cors_headers(req);
  if (!fan_timer_running()) {
    httpd_resp_set_status(req, "409 Conflict");
    httpd_resp_sendstr(req, "{\"result\":false,\"error\":\"no timer running\"}");
    return ESP_OK;
  }
  cancel_fan_timer();
  httpd_resp_sendstr(req, "{\"result\":true\"}");
  return ESP_OK;
}

// /api/status 处理函数
static esp_err_t api_status_handler(httpd_req_t *req) {
  set_cors_headers(req);
  int isON = get_fan_isON();
  int timer_left = fan_timer_left();
  char resp[128];
  int len = snprintf(resp, sizeof(resp), "{\"status\":%d,\"timer_left\":%d}", isON, timer_left);
  ESP_LOGI(TAG, "/api/status called, status=%d, timer_left=%d, resp=%.*s", isON, timer_left, len,
           resp);
  httpd_resp_sendstr(req, resp);
  return ESP_OK;
}
// / 主页处理函数，直接返回 index.html 内容，不跳转
static esp_err_t index_html_handler(httpd_req_t *req) {
  char filepath[128] = "/spiffs/index.html";
  FILE *f = fopen(filepath, "r");
  if (!f) {
    ESP_LOGW(TAG, "File not found: %s", filepath);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "text/html");
  char buf[256];
  size_t read_bytes;
  while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0) {
    httpd_resp_send_chunk(req, buf, read_bytes);
  }
  fclose(f);
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

// 新增静态文件通用处理函数
static esp_err_t static_file_handler(httpd_req_t *req) {
  char filepath[128] = "/spiffs";
  strncat(filepath, req->uri, sizeof(filepath) - strlen(filepath) - 1);
  ESP_LOGI(TAG, "[STATIC] Try open: %s", filepath);
  FILE *f = fopen(filepath, "r");
  if (!f) {
    ESP_LOGW(TAG, "File not found: %s", filepath);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  char buf[256];
  size_t read_bytes;
  // 简单类型判断
  if (strstr(filepath, ".html"))
    httpd_resp_set_type(req, "text/html");
  else if (strstr(filepath, ".js"))
    httpd_resp_set_type(req, "application/javascript");
  else if (strstr(filepath, ".css"))
    httpd_resp_set_type(req, "text/css");
  else if (strstr(filepath, ".json"))
    httpd_resp_set_type(req, "application/json");
  else if (strstr(filepath, ".ico"))
    httpd_resp_set_type(req, "image/x-icon");
  else
    httpd_resp_set_type(req, "text/plain");
  while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0) {
    httpd_resp_send_chunk(req, buf, read_bytes);
  }
  fclose(f);
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

void start_http_server() {
  // 启动 HTTP 服务器前先挂载 SPIFFS
  esp_vfs_spiffs_conf_t conf = {.base_path = "/spiffs",
                                .partition_label = NULL,
                                .max_files = 5,
                                .format_if_mount_failed = true};
  esp_vfs_spiffs_register(&conf);
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 8080;
  config.uri_match_fn = httpd_uri_match_wildcard; // 关键修正，支持 /* 匹配所有静态资源
  httpd_handle_t server = NULL;
  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t on_uri = {
        .uri = "/api/on", .method = HTTP_GET, .handler = api_on_handler, .user_ctx = NULL};
    httpd_uri_t off_uri = {
        .uri = "/api/off", .method = HTTP_GET, .handler = api_off_handler, .user_ctx = NULL};
    httpd_uri_t status_uri = {
        .uri = "/api/status", .method = HTTP_GET, .handler = api_status_handler, .user_ctx = NULL};
    httpd_uri_t index_uri = {
        .uri = "/", .method = HTTP_GET, .handler = index_html_handler, .user_ctx = NULL};
    // 统一 CORS 预检处理，匹配所有 /api/ 路径
    httpd_uri_t cors_api_uri = {.uri = "/api/*",
                                .method = HTTP_OPTIONS,
                                .handler = cors_preflight_handler,
                                .user_ctx = NULL};
    // 用 /* 通配符统一处理所有静态文件
    httpd_uri_t static_file_uri = {
        .uri = "/*", .method = HTTP_GET, .handler = static_file_handler, .user_ctx = NULL};
    httpd_uri_t timer_off_uri = {.uri = "/api/timer_off",
                                 .method = HTTP_GET,
                                 .handler = api_timer_off_handler,
                                 .user_ctx = NULL};
    httpd_uri_t cancel_timer_uri = {.uri = "/api/cancel_timer",
                                    .method = HTTP_GET,
                                    .handler = api_cancel_timer_handler,
                                    .user_ctx = NULL};
    // api
    httpd_register_uri_handler(server, &cors_api_uri);
    httpd_register_uri_handler(server, &on_uri);
    httpd_register_uri_handler(server, &off_uri);
    httpd_register_uri_handler(server, &status_uri);
    httpd_register_uri_handler(server, &timer_off_uri);
    httpd_register_uri_handler(server, &cancel_timer_uri);
    // static files
    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &static_file_uri);
  }
}
