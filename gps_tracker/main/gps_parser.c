#include "gps_parser.h"
#include "esp_log.h"
#include "driver/uart.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "gps_parser";
static int s_uart_num = UART_NUM_1;
static gps_data_t s_latest_data = {0};
static gps_raw_t s_raw_data = {0};
static SemaphoreHandle_t s_data_mutex = NULL;

/* NMEA校验和验证 */
static bool nmea_checksum(const char *nmea)
{
    if (*nmea != '$') return false;
    const char *ck_start = strchr(nmea, '*');
    if (!ck_start) return false;
    unsigned char calc = 0;
    for (const char *p = nmea + 1; p < ck_start; p++) calc ^= *p;
    unsigned int msg_ck;
    sscanf(ck_start + 1, "%02x", &msg_ck);
    return calc == msg_ck;
}

/* 解析RMC语句 */
static bool parse_rmc(const char *line, gps_data_t *data)
{
    if (strstr(line, "RMC") == NULL) return false;
    char buf[120];
    strncpy(buf, line, sizeof(buf) - 1);
    char *p = buf;
    /* 跳过地址字段 */
    p = strchr(p, ',');
    if (!p) return false;
    p++;
    /* 字段1: UTC时间 → 转为北京时间 */
    char *tok = p;
    p = strchr(p, ',');
    if (p) {
        *p = 0;
        int h = 0, m = 0, s = 0;
        char frac[4] = "000";
        sscanf(tok, "%2d%2d%2d.%3s", &h, &m, &s, frac);
        h = (h + 8) % 24;
        snprintf(data->bj_time, sizeof(data->bj_time), "%02d%02d%02d.%s", h, m, s, frac);
        p++;
    }
    /* 字段2: 状态 */
    tok = p; p = strchr(p, ',');
    if (p) { *p = 0; data->status = *tok; p++; }
    /* 字段3: 纬度 ddmm.mmmm */
    tok = p; p = strchr(p, ',');
    if (p) { *p = 0; if (strlen(tok) > 0) {
        double ddmm = atof(tok);
        int deg = (int)(ddmm / 100);
        double min = ddmm - deg * 100;
        data->latitude = deg + min / 60.0;
    } p++; }
    /* 字段4: N/S */
    tok = p; p = strchr(p, ',');
    if (p) { *p = 0; data->latitude_ns = *tok; if (*tok == 'S') data->latitude = -data->latitude; p++; }
    /* 字段5: 经度 dddmm.mmmm */
    tok = p; p = strchr(p, ',');
    if (p) { *p = 0; if (strlen(tok) > 0) {
        double dddmm = atof(tok);
        int deg = (int)(dddmm / 100);
        double min = dddmm - deg * 100;
        data->longitude = deg + min / 60.0;
    } p++; }
    /* 字段6: E/W */
    tok = p; p = strchr(p, ',');
    if (p) { *p = 0; data->longitude_ew = *tok; if (*tok == 'W') data->longitude = -data->longitude; p++; }
    /* 字段7: 速度 (节) */
    tok = p; p = strchr(p, ',');
    if (p) { *p = 0; data->speed_knots = atof(tok); p++; }
    /* 字段8: 航向 */
    tok = p; p = strchr(p, ',');
    if (p) { *p = 0; data->course = atof(tok); p++; }
    /* 字段9: 日期 */
    tok = p; p = strchr(p, ',');
    if (p) { *p = 0; strncpy(data->date, tok, sizeof(data->date) - 1); p++; }
    /* 字段10: 磁偏角 */
    tok = p; p = strchr(p, ',');
    if (p) { *p = 0; data->magnetic_variation = atof(tok); p++; }
    /* 字段11: 磁偏角方向 */
    tok = p; p = strchr(p, ',');
    if (p) { *p = 0; data->magnetic_variation_dir = *tok; p++; }
    /* 字段12: 模式 */
    tok = p; p = strchr(p, '*');
    if (p) { *p = 0; data->mode = *tok; }
    /* 计算换算速度 */
    data->speed_kmh = data->speed_knots * 1.852;
    data->speed_ms = data->speed_knots * 0.514444;
    return true;
}

/* 解析GGA语句（卫星数、海拔） */
static void parse_gga(const char *line, gps_data_t *data)
{
    if (strstr(line, "GGA") == NULL) return;
    char buf[120];
    strncpy(buf, line, sizeof(buf) - 1);
    int field = 0;
    for (char *p = buf; p && *p; p = strchr(p + 1, ',')) {
        field++;
        if (field == 7) { /* 卫星数 */
            char *v = p + 1;
            char *end = strchr(v, ',');
            if (end) { *end = 0; data->satellites = atoi(v); }
        } else if (field == 8) { /* HDOP */
            char *v = p + 1;
            char *end = strchr(v, ',');
            if (end) { *end = 0; data->hdop = atof(v); }
        } else if (field == 9) { /* 海拔 */
            char *v = p + 1;
            char *end = strchr(v, ',');
            if (end) { *end = 0; data->altitude = atof(v); }
        }
    }
}

/* UART读取任务 */
static void uart_read_task(void *arg)
{
    uint8_t buf[512];
    char line[128];
    int line_pos = 0;
    while (1) {
        int len = uart_read_bytes(s_uart_num, buf, sizeof(buf) - 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            buf[len] = 0;
            for (int i = 0; i < len; i++) {
                char c = buf[i];
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line[line_pos] = 0;
                        if (line[0] == '$' && nmea_checksum(line)) {
                            if (s_data_mutex) xSemaphoreTake(s_data_mutex, portMAX_DELAY);
                            if (strstr(line, "RMC")) {
                                parse_rmc(line, &s_latest_data);
                                strncpy(s_raw_data.raw_rmc, line, sizeof(s_raw_data.raw_rmc) - 1);
                            } else if (strstr(line, "GGA")) {
                                parse_gga(line, &s_latest_data);
                                strncpy(s_raw_data.raw_gga, line, sizeof(s_raw_data.raw_gga) - 1);
                            } else if (strstr(line, "GSA")) {
                                strncpy(s_raw_data.raw_gsa, line, sizeof(s_raw_data.raw_gsa) - 1);
                            } else if (strstr(line, "GSV")) {
                                strncpy(s_raw_data.raw_gsv, line, sizeof(s_raw_data.raw_gsv) - 1);
                            } else if (strstr(line, "VTG")) {
                                strncpy(s_raw_data.raw_vtg, line, sizeof(s_raw_data.raw_vtg) - 1);
                            } else if (strstr(line, "ZDA")) {
                                strncpy(s_raw_data.raw_zda, line, sizeof(s_raw_data.raw_zda) - 1);
                            }
                            if (s_data_mutex) xSemaphoreGive(s_data_mutex);
                        }
                        line_pos = 0;
                    }
                } else if (c == '$') {
                    line_pos = 0;
                    line[line_pos++] = c;
                } else if (line_pos > 0 && line_pos < (int)sizeof(line) - 1) {
                    line[line_pos++] = c;
                }
            }
        }
    }
}

esp_err_t gps_parser_init(int uart_num, int baud_rate, int tx_pin, int rx_pin)
{
    s_uart_num = uart_num;
    s_data_mutex = xSemaphoreCreateMutex();

    uart_config_t uart_config = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(uart_num, &uart_config);
    uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(uart_num, 2048, 0, 0, NULL, 0);

    xTaskCreatePinnedToCore(uart_read_task, "gps_uart", 4096, NULL, 10, NULL, 0);
    ESP_LOGI(TAG, "GPS UART初始化完成, 波特率: %d", baud_rate);
    return ESP_OK;
}

bool gps_parser_parse(gps_data_t *data)
{
    return false; /* 现在由后台任务处理 */
}

esp_err_t gps_parser_send_command(const char *cmd)
{
    size_t len = strlen(cmd);
    uart_write_bytes(s_uart_num, cmd, len);
    uart_write_bytes(s_uart_num, "\r\n", 2);
    ESP_LOGI(TAG, "发送指令: %s", cmd);
    return ESP_OK;
}

esp_err_t gps_parser_set_baud(int baud_idx)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "$CCCAS,1,%d*XX", baud_idx);
    /* 计算校验和 */
    unsigned char ck = 0;
    for (const char *p = cmd + 1; *p && *p != '*'; p++) ck ^= *p;
    snprintf(cmd, sizeof(cmd), "$CCCAS,1,%d*%02X", baud_idx, ck);
    return gps_parser_send_command(cmd);
}

void gps_parser_get_latest(gps_data_t *data)
{
    if (s_data_mutex) xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    memcpy(data, &s_latest_data, sizeof(gps_data_t));
    if (s_data_mutex) xSemaphoreGive(s_data_mutex);
}

void gps_parser_get_raw(gps_raw_t *raw)
{
    if (s_data_mutex) xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    memcpy(raw, &s_raw_data, sizeof(gps_raw_t));
    if (s_data_mutex) xSemaphoreGive(s_data_mutex);
}

void gps_parser_set_grid(const char *grid_id)
{
    if (s_data_mutex) xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    strncpy(s_latest_data.grid, grid_id, sizeof(s_latest_data.grid) - 1);
    if (s_data_mutex) xSemaphoreGive(s_data_mutex);
}
