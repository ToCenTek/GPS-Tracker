#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "wifi_manager";
static char s_sta_ip[16] = "0.0.0.0";
static char s_connected_ssid[33] = {0};
static bool s_sta_connected = false;
static char s_pending_ssid[33] = {0};
static char s_pending_pwd[65] = {0};
static bool s_connecting_new = false;
static esp_netif_t *s_ap_netif = NULL;
static esp_netif_t *s_sta_netif = NULL;

/* 保存STA凭据到NVS */
static void save_cred(const char *ssid, const char *pwd)
{
    nvs_handle_t h;
    if (nvs_open("wifi_cfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "sta_ssid", ssid);
        nvs_set_str(h, "sta_pwd", pwd ? pwd : "");
        nvs_commit(h);
        nvs_close(h);
    }
}

/* 事件处理 */
static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_AP_STACONNECTED)
            ESP_LOGI(TAG, "新设备连接到AP");
        else if (id == WIFI_EVENT_AP_STADISCONNECTED)
            ESP_LOGI(TAG, "设备断开AP连接");
        else if (id == WIFI_EVENT_STA_START)
            esp_wifi_connect();
        else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            s_sta_connected = false;
            memset(s_sta_ip, 0, sizeof(s_sta_ip));
            strcpy(s_sta_ip, "0.0.0.0");
            if (s_connecting_new) {
                ESP_LOGI(TAG, "STA断开, 正在尝试新连接");
                return;
            }
            ESP_LOGI(TAG, "STA意外断开, 尝试重连");
            char ssid[33] = {0}, pwd[65] = {0};
            nvs_handle_t h;
            if (nvs_open("wifi_cfg", NVS_READONLY, &h) == ESP_OK) {
                size_t len = sizeof(ssid);
                nvs_get_str(h, "sta_ssid", ssid, &len);
                len = sizeof(pwd);
                nvs_get_str(h, "sta_pwd", pwd, &len);
                nvs_close(h);
            }
            if (strlen(ssid)) {
                wifi_config_t cfg = {0};
                strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
                strncpy((char *)cfg.sta.password, pwd, sizeof(cfg.sta.password) - 1);
                esp_wifi_set_config(WIFI_IF_STA, &cfg);
                esp_wifi_connect();
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        esp_ip4addr_ntoa(&ev->ip_info.ip, s_sta_ip, sizeof(s_sta_ip));
        s_sta_connected = true;
        s_connecting_new = false;
        ESP_LOGI(TAG, "STA获取IP: %s", s_sta_ip);
        /* 连接成功后才保存凭据到NVS */
        if (strlen(s_pending_ssid)) {
            save_cred(s_pending_ssid, s_pending_pwd);
            memset(s_pending_ssid, 0, sizeof(s_pending_ssid));
            memset(s_pending_pwd, 0, sizeof(s_pending_pwd));
        }
    }
}

esp_err_t wifi_manager_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();

    s_ap_netif = esp_netif_create_default_wifi_ap();
    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL);

    /* AP配置 */
    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .password = WIFI_AP_PASSWORD,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };
    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);

    /* STA配置 (有凭据则连接) */
    char ssid[33] = {0}, pwd[65] = {0};
    nvs_handle_t h;
    if (nvs_open("wifi_cfg", NVS_READONLY, &h) == ESP_OK) {
        size_t ssid_len = sizeof(ssid);
        size_t pwd_len = sizeof(pwd);
        nvs_get_str(h, "sta_ssid", ssid, &ssid_len);
        nvs_get_str(h, "sta_pwd", pwd, &pwd_len);
        nvs_close(h);
    }
    if (strlen(ssid)) {
        wifi_config_t sta_cfg = {0};
        strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
        strncpy((char *)sta_cfg.sta.password, pwd, sizeof(sta_cfg.sta.password) - 1);
        esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
        strncpy(s_connected_ssid, ssid, sizeof(s_connected_ssid) - 1);
        ESP_LOGI(TAG, "STA已配置SSID: %s", ssid);
    }

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_start();

    ESP_LOGI(TAG, "WiFi AP+STA启动完成, SSID: %s", WIFI_AP_SSID);
    return ESP_OK;
}

bool wifi_manager_is_sta_connected(void)
{
    return s_sta_connected;
}

esp_err_t wifi_manager_sta_connect(const char *ssid, const char *password)
{
    s_connecting_new = true;
    strncpy(s_pending_ssid, ssid, sizeof(s_pending_ssid) - 1);
    if (password) strncpy(s_pending_pwd, password, sizeof(s_pending_pwd) - 1);
    strncpy(s_connected_ssid, ssid, sizeof(s_connected_ssid) - 1);
    s_sta_connected = false;

    wifi_config_t sta_cfg = {0};
    strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
    if (password) strncpy((char *)sta_cfg.sta.password, password, sizeof(sta_cfg.sta.password) - 1);
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_connect();
    ESP_LOGI(TAG, "正在连接STA: %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_sta_disconnect(void)
{
    save_cred("", "");
    s_sta_connected = false;
    memset(s_connected_ssid, 0, sizeof(s_connected_ssid));
    memset(s_sta_ip, 0, sizeof(s_sta_ip));
    strcpy(s_sta_ip, "0.0.0.0");
    esp_wifi_disconnect();
    ESP_LOGI(TAG, "STA已断开");
    return ESP_OK;
}

esp_err_t wifi_manager_scan(char *result_buf, size_t buf_len)
{
    uint16_t count = 0;
    esp_err_t err = esp_wifi_scan_start(NULL, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi 扫描失败: %s", esp_err_to_name(err));
        snprintf(result_buf, buf_len, "{\"ap_list\":[]}");
        return err;
    }
    err = esp_wifi_scan_get_ap_num(&count);
    if (err != ESP_OK) {
        snprintf(result_buf, buf_len, "{\"ap_list\":[]}");
        return err;
    }
    if (count == 0) {
        snprintf(result_buf, buf_len, "{\"ap_list\":[]}");
        return ESP_OK;
    }
    wifi_ap_record_t *rec = malloc(count * sizeof(wifi_ap_record_t));
    if (!rec) { snprintf(result_buf, buf_len, "{\"ap_list\":[]}"); return ESP_ERR_NO_MEM; }
    esp_wifi_scan_get_ap_records(&count, rec);

    size_t pos = 0;
    pos += snprintf(result_buf + pos, buf_len - pos, "{\"ap_list\":[");
    for (int i = 0; i < count && i < 40; i++) {
        const char *auth = "OPEN";
        switch (rec[i].authmode) {
        case WIFI_AUTH_WEP: auth = "WEP"; break;
        case WIFI_AUTH_WPA_PSK: auth = "WPA"; break;
        case WIFI_AUTH_WPA2_PSK: auth = "WPA2"; break;
        case WIFI_AUTH_WPA_WPA2_PSK: auth = "WPA/WPA2"; break;
        case WIFI_AUTH_WPA3_PSK: auth = "WPA3"; break;
        default: break;
        }
        pos += snprintf(result_buf + pos, buf_len - pos,
            "%s{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":\"%s\",\"encrypted\":%s}",
            i > 0 ? "," : "", rec[i].ssid, rec[i].rssi, auth,
            rec[i].authmode == WIFI_AUTH_OPEN ? "false" : "true");
    }
    pos += snprintf(result_buf + pos, buf_len - pos, "]}");
    free(rec);
    return ESP_OK;
}

const char *wifi_manager_get_sta_ip(void)
{
    return s_sta_ip;
}

const char *wifi_manager_get_ssid(void)
{
    return s_connected_ssid;
}
