#include "http_server.h"
#include "gps_parser.h"
#include "grid_manager.h"
#include "wifi_manager.h"
#include "receiver_config.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <cJSON.h>

static const char *TAG = "http_server";
static httpd_handle_t s_server = NULL;

/* 嵌入式文件 */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t leaflet_js_start[] asm("_binary_leaflet_js_start");
extern const uint8_t leaflet_js_end[] asm("_binary_leaflet_js_end");
extern const uint8_t leaflet_css_start[] asm("_binary_leaflet_css_start");
extern const uint8_t leaflet_css_end[] asm("_binary_leaflet_css_end");

/* 生成GPS JSON */
static void build_gps_json(char *buf, size_t len)
{
    gps_data_t gps;
    gps_parser_get_latest(&gps);

    /* 查询网格ID (优先查多边形格子) */
    char grid_id[20] = "0";
    if (gps.status == 'A' && gps.latitude != 0) {
        grid_manager_query(gps.latitude, gps.longitude, grid_id, sizeof(grid_id));
        gps_parser_set_grid(grid_id);
    }

    char lat_str[32], lng_str[32], mag_str[16];
    snprintf(lat_str, sizeof(lat_str), "%.6f %c", gps.latitude, gps.latitude_ns ? gps.latitude_ns : 'N');
    snprintf(lng_str, sizeof(lng_str), "%.6f %c", gps.longitude, gps.longitude_ew ? gps.longitude_ew : 'E');
    snprintf(mag_str, sizeof(mag_str), "%.1f %c", gps.magnetic_variation, gps.magnetic_variation_dir ? gps.magnetic_variation_dir : 'E');

    char st = gps.mode ? gps.mode : (gps.status == 'A' ? 'A' : 'N');
    snprintf(buf, len,
        "{\n"
        "  \"bj_time\":\"%s\",\n"
        "  \"status\":\"%c\",\n"
        "  \"latitude\":\"%s\",\n"
        "  \"longitude\":\"%s\",\n"
        "  \"speed_knots\":%.2f,\n"
        "  \"speed_kmh\":%.2f,\n"
        "  \"speed_ms\":%.2f,\n"
        "  \"course\":%.1f,\n"
        "  \"date\":\"%s\",\n"
        "  \"magnetic_variation\":\"%s\",\n"
        "  \"altitude\":%.1f,\n"
        "  \"satellites\":%d,\n"
        "  \"grid\":\"%s\"\n"
        "}",
        gps.bj_time,
        st,
        lat_str,
        lng_str,
        gps.speed_knots,
        gps.speed_kmh,
        gps.speed_ms,
        gps.course,
        gps.date,
        mag_str,
        gps.altitude,
        gps.satellites,
        grid_id);
}

/* GET /gps */
static esp_err_t gps_json_handler(httpd_req_t *req)
{
    char buf[1024];
    build_gps_json(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

/* GET /grid/data - 完整网格数据 (用于页面加载恢复) */
static esp_err_t grid_data_handler(httpd_req_t *req)
{
    char *json = grid_manager_get_cells_json();
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

/* GET /grid - 当前GPS所在的网格和位置（精简易懂版） */
static esp_err_t grid_get_handler(httpd_req_t *req)
{
    gps_data_t gps;
    gps_parser_get_latest(&gps);
    char grid_id[20] = "0";
    char lat_str[32] = "0.000000 N", lng_str[32] = "0.000000 E";
    if (gps.status == 'A' && gps.latitude != 0) {
        grid_manager_query(gps.latitude, gps.longitude, grid_id, sizeof(grid_id));
        snprintf(lat_str, sizeof(lat_str), "%.6f %c", gps.latitude, gps.latitude_ns ? gps.latitude_ns : 'N');
        snprintf(lng_str, sizeof(lng_str), "%.6f %c", gps.longitude, gps.longitude_ew ? gps.longitude_ew : 'E');
    }
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"latitude\":\"%s\",\"longitude\":\"%s\",\"grid\":\"%s\"}",
        lat_str, lng_str, grid_id);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

/* POST /grid - 创建网格 (参数化) */
static esp_err_t grid_post_handler(httpd_req_t *req)
{
    char buf[4096];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no data"); return ESP_FAIL; }
    buf[ret] = 0;
    cJSON *root = cJSON_Parse(buf);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json"); return ESP_FAIL; }
    cJSON *size_item = cJSON_GetObjectItem(root, "grid_size");
    cJSON *poly_item = cJSON_GetObjectItem(root, "polygon");
    if (!size_item || !poly_item || !cJSON_IsArray(poly_item)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing grid_size or polygon");
        return ESP_FAIL;
    }
    double sz = size_item->valuedouble;
    double corners[4][2];
    int count = cJSON_GetArraySize(poly_item);
    if (count > 4) count = 4;
    for (int i = 0; i < count; i++) {
        cJSON *pt = cJSON_GetArrayItem(poly_item, i);
        if (pt && cJSON_IsArray(pt) && cJSON_GetArraySize(pt) >= 2) {
            corners[i][0] = cJSON_GetArrayItem(pt, 1)->valuedouble; /* lat */
            corners[i][1] = cJSON_GetArrayItem(pt, 0)->valuedouble; /* lng */
        }
    }
    /* 不足4个角则用第1个填充 */
    for (int i = count; i < 4; i++) memcpy(corners[i], corners[0], 16);
    grid_manager_generate(corners, sz);
    cJSON_Delete(root);
    char *json = grid_manager_get_json();
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

/* POST /config - NMEA配置 */
static esp_err_t config_post_handler(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no data");
        return ESP_FAIL;
    }
    buf[ret] = 0;
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_FAIL;
    }
    cJSON *raw_item = cJSON_GetObjectItem(root, "raw");
    if (raw_item && cJSON_IsString(raw_item)) {
        receiver_send_command(raw_item->valuestring);
    }
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send(req, "{\"ok\":true}", strlen("{\"ok\":true}"));
    return ESP_OK;
}

/* GET /nmea - 原始NMEA语句 */
static esp_err_t nmea_handler(httpd_req_t *req)
{
    gps_raw_t raw;
    gps_parser_get_raw(&raw);
    char buf[1024];
    snprintf(buf, sizeof(buf),
        "{\"rmc\":\"%s\",\"gga\":\"%s\",\"gsa\":\"%s\",\"gsv\":\"%s\",\"vtg\":\"%s\",\"zda\":\"%s\"}",
        raw.raw_rmc, raw.raw_gga, raw.raw_gsa, raw.raw_gsv, raw.raw_vtg, raw.raw_zda);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

/* GET /wifi/status */
static esp_err_t wifi_status_handler(httpd_req_t *req)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"mode\":\"ap+sta\",\"station\":\"%s\",\"softap\":\"192.168.4.1\",\"station_ssid\":\"%s\",\"softap_ssid\":\"GPS\",\"connected\":%s}",
        wifi_manager_get_sta_ip(), wifi_manager_get_ssid(),
        wifi_manager_is_sta_connected() ? "true" : "false");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

/* GET /wifi/scan */
static esp_err_t wifi_scan_handler(httpd_req_t *req)
{
    static char buf[4096]; /* 静态缓冲避免栈溢出 */
    wifi_manager_scan(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

/* POST /wifi/connect */
static esp_err_t wifi_connect_handler(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no data");
        return ESP_FAIL;
    }
    buf[ret] = 0;
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_FAIL;
    }
    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *pwd = cJSON_GetObjectItem(root, "password");
    if (ssid && cJSON_IsString(ssid)) {
        wifi_manager_sta_connect(ssid->valuestring, pwd ? pwd->valuestring : "");
    }
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send(req, "{\"ok\":true}", strlen("{\"ok\":true}"));
    return ESP_OK;
}
/* POST /wifi/disconnect */
static esp_err_t wifi_disconnect_handler(httpd_req_t *req)
{
    wifi_manager_sta_disconnect();
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send(req, "{\"ok\":true}", strlen("{\"ok\":true}"));
    return ESP_OK;
}

/* 首页 */
static esp_err_t index_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

/* /leaflet.js */
static esp_err_t leaflet_js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, (const char *)leaflet_js_start, leaflet_js_end - leaflet_js_start);
    return ESP_OK;
}

/* /leaflet.css */
static esp_err_t leaflet_css_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, (const char *)leaflet_css_start, leaflet_css_end - leaflet_css_start);
    return ESP_OK;
}

/* 注册路由 */
static void register_handlers(httpd_handle_t server)
{
    httpd_uri_t uri_root = {.uri = "/", .method = HTTP_GET, .handler = index_handler};
    httpd_register_uri_handler(server, &uri_root);

    httpd_uri_t uri_ljs = {.uri = "/leaflet.js", .method = HTTP_GET, .handler = leaflet_js_handler};
    httpd_register_uri_handler(server, &uri_ljs);

    httpd_uri_t uri_lcss = {.uri = "/leaflet.css", .method = HTTP_GET, .handler = leaflet_css_handler};
    httpd_register_uri_handler(server, &uri_lcss);

    httpd_uri_t uri_gps = {.uri = "/gps", .method = HTTP_GET, .handler = gps_json_handler};
    httpd_register_uri_handler(server, &uri_gps);
    httpd_uri_t uri_gps_json = {.uri = "/gps.json", .method = HTTP_GET, .handler = gps_json_handler};
    httpd_register_uri_handler(server, &uri_gps_json);

    httpd_uri_t uri_nmea = {.uri = "/nmea", .method = HTTP_GET, .handler = nmea_handler};
    httpd_register_uri_handler(server, &uri_nmea);

    httpd_uri_t uri_grid_post = {.uri = "/grid", .method = HTTP_POST, .handler = grid_post_handler};
    httpd_register_uri_handler(server, &uri_grid_post);
    httpd_uri_t uri_grid_get = {.uri = "/grid", .method = HTTP_GET, .handler = grid_get_handler};
    httpd_register_uri_handler(server, &uri_grid_get);
    httpd_uri_t uri_grid_data = {.uri = "/grid/data", .method = HTTP_GET, .handler = grid_data_handler};
    httpd_register_uri_handler(server, &uri_grid_data);

    httpd_uri_t uri_config = {.uri = "/config", .method = HTTP_POST, .handler = config_post_handler};
    httpd_register_uri_handler(server, &uri_config);

    httpd_uri_t uri_wifi_st = {.uri = "/wifi/status", .method = HTTP_GET, .handler = wifi_status_handler};
    httpd_register_uri_handler(server, &uri_wifi_st);

    httpd_uri_t uri_wifi_scan = {.uri = "/wifi/scan", .method = HTTP_GET, .handler = wifi_scan_handler};
    httpd_register_uri_handler(server, &uri_wifi_scan);

    httpd_uri_t uri_wifi_con = {.uri = "/wifi/connect", .method = HTTP_POST, .handler = wifi_connect_handler};
    httpd_register_uri_handler(server, &uri_wifi_con);

    httpd_uri_t uri_wifi_dis = {.uri = "/wifi/disconnect", .method = HTTP_POST, .handler = wifi_disconnect_handler};
    httpd_register_uri_handler(server, &uri_wifi_dis);
}

esp_err_t http_server_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 20;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err == ESP_OK) {
        register_handlers(s_server);
        ESP_LOGI(TAG, "HTTP服务器启动: 端口 80");
    } else {
        ESP_LOGE(TAG, "HTTP服务器启动失败: %s", esp_err_to_name(err));
    }
    return err;
}
