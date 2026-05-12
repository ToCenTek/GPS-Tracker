#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

#define WIFI_AP_SSID       "GPS"
#define WIFI_AP_PASSWORD   CONFIG_WIFI_AP_PASSWORD
#define WIFI_AP_MAX_CONN   4

/* 初始化WiFi (AP+STA模式) */
esp_err_t wifi_manager_init(void);

/* 是否已连接STA */
bool wifi_manager_is_sta_connected(void);

/* 连接STA网络 */
esp_err_t wifi_manager_sta_connect(const char *ssid, const char *password);

/* 断开STA */
esp_err_t wifi_manager_sta_disconnect(void);

/* 扫描WiFi热点 (返回JSON字符串) */
esp_err_t wifi_manager_scan(char *result_buf, size_t buf_len);

/* 获取STA IP */
const char *wifi_manager_get_sta_ip(void);

/* 获取已连接SSID */
const char *wifi_manager_get_ssid(void);

#endif
