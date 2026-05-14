#include "oled_display.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "oled";
static uint8_t s_fb[1024];
static int s_i2c_ok = 1;
static uint32_t s_boot_ms = 0;

/* ==================== 6x8 ASCII字库 ==================== */
static const uint8_t font[95][6] = {
    {0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00,0x00},{0x00,0x07,0x00,0x07,0x00,0x00},{0x14,0x7F,0x14,0x7F,0x14,0x00},
    {0x24,0x2A,0x7F,0x2A,0x12,0x00},{0x23,0x13,0x08,0x64,0x62,0x00},{0x36,0x49,0x55,0x22,0x50,0x00},{0x00,0x05,0x03,0x00,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00,0x00},{0x00,0x41,0x22,0x1C,0x00,0x00},{0x08,0x2A,0x1C,0x2A,0x08,0x00},{0x08,0x08,0x3E,0x08,0x08,0x00},
    {0x00,0x50,0x30,0x00,0x00,0x00},{0x08,0x08,0x08,0x08,0x08,0x00},{0x00,0x60,0x60,0x00,0x00,0x00},{0x20,0x10,0x08,0x04,0x02,0x00},
    {0x3E,0x51,0x49,0x45,0x3E,0x00},{0x00,0x42,0x7F,0x40,0x00,0x00},{0x42,0x61,0x51,0x49,0x46,0x00},{0x21,0x41,0x45,0x4B,0x31,0x00},
    {0x18,0x14,0x12,0x7F,0x10,0x00},{0x27,0x45,0x45,0x45,0x39,0x00},{0x3C,0x4A,0x49,0x49,0x30,0x00},{0x01,0x71,0x09,0x05,0x03,0x00},
    {0x36,0x49,0x49,0x49,0x36,0x00},{0x06,0x49,0x49,0x29,0x1E,0x00},{0x00,0x36,0x36,0x00,0x00,0x00},{0x00,0x56,0x36,0x00,0x00,0x00},
    {0x00,0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14,0x00},{0x41,0x22,0x14,0x08,0x00,0x00},{0x02,0x01,0x51,0x09,0x06,0x00},
    {0x32,0x49,0x79,0x41,0x3E,0x00},{0x7E,0x11,0x11,0x11,0x7E,0x00},{0x7F,0x49,0x49,0x49,0x36,0x00},{0x3E,0x41,0x41,0x41,0x22,0x00},
    {0x7F,0x41,0x41,0x22,0x1C,0x00},{0x7F,0x49,0x49,0x49,0x41,0x00},{0x7F,0x09,0x09,0x01,0x01,0x00},{0x3E,0x41,0x41,0x51,0x32,0x00},
    {0x7F,0x08,0x08,0x08,0x7F,0x00},{0x00,0x41,0x7F,0x41,0x00,0x00},{0x20,0x40,0x41,0x3F,0x01,0x00},{0x7F,0x08,0x14,0x22,0x41,0x00},
    {0x7F,0x40,0x40,0x40,0x40,0x00},{0x7F,0x02,0x04,0x02,0x7F,0x00},{0x7F,0x04,0x08,0x10,0x7F,0x00},{0x3E,0x41,0x41,0x41,0x3E,0x00},
    {0x7F,0x09,0x09,0x09,0x06,0x00},{0x3E,0x41,0x51,0x21,0x5E,0x00},{0x7F,0x09,0x19,0x29,0x46,0x00},{0x46,0x49,0x49,0x49,0x31,0x00},
    {0x01,0x01,0x7F,0x01,0x01,0x00},{0x3F,0x40,0x40,0x40,0x3F,0x00},{0x1F,0x20,0x40,0x20,0x1F,0x00},{0x7F,0x20,0x18,0x20,0x7F,0x00},
    {0x63,0x14,0x08,0x14,0x63,0x00},{0x03,0x04,0x78,0x04,0x03,0x00},{0x61,0x51,0x49,0x45,0x43,0x00},{0x00,0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20,0x00},{0x41,0x41,0x7F,0x00,0x00,0x00},{0x04,0x02,0x01,0x02,0x04,0x00},{0x40,0x40,0x40,0x40,0x40,0x00},
    {0x00,0x01,0x02,0x04,0x00,0x00},{0x20,0x54,0x54,0x54,0x78,0x00},{0x7F,0x48,0x44,0x44,0x38,0x00},{0x38,0x44,0x44,0x44,0x20,0x00},
    {0x38,0x44,0x44,0x48,0x7F,0x00},{0x38,0x54,0x54,0x54,0x18,0x00},{0x08,0x7E,0x09,0x01,0x02,0x00},{0x08,0x14,0x54,0x54,0x3C,0x00},
    {0x7F,0x08,0x04,0x04,0x78,0x00},{0x00,0x44,0x7D,0x40,0x00,0x00},{0x20,0x40,0x44,0x3D,0x00,0x00},{0x00,0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00,0x00},{0x7C,0x04,0x18,0x04,0x78,0x00},{0x7C,0x08,0x04,0x04,0x78,0x00},{0x38,0x44,0x44,0x44,0x38,0x00},
    {0x7C,0x14,0x14,0x14,0x08,0x00},{0x08,0x14,0x14,0x18,0x7C,0x00},{0x7C,0x08,0x04,0x04,0x08,0x00},{0x48,0x54,0x54,0x54,0x20,0x00},
    {0x04,0x3F,0x44,0x40,0x20,0x00},{0x3C,0x40,0x40,0x20,0x7C,0x00},{0x1C,0x20,0x40,0x20,0x1C,0x00},{0x3C,0x40,0x30,0x40,0x3C,0x00},
    {0x44,0x28,0x10,0x28,0x44,0x00},{0x0C,0x50,0x50,0x50,0x3C,0x00},{0x44,0x64,0x54,0x4C,0x44,0x00},
};

/* ==================== 软件I2C (bit-bang) ==================== */
#define PIN_SDA GPIO_NUM_21
#define PIN_SCL GPIO_NUM_22
#define I2C_ADDR 0x3C

static void i2c_dly(void) {
    for (volatile int i = 0; i < 120; i++); /* ~10us @ 240MHz */
}
#define SDA_L() gpio_set_level(PIN_SDA, 0)
#define SDA_H() gpio_set_level(PIN_SDA, 1)
#define SCL_L() gpio_set_level(PIN_SCL, 0)
#define SCL_H() gpio_set_level(PIN_SCL, 1)
#define SDA_R() gpio_get_level(PIN_SDA)

static void i2c_start(void) {
    SDA_H(); SCL_H(); i2c_dly();
    SDA_L(); i2c_dly(); SCL_L();
}
static void i2c_stop(void) {
    SDA_L(); i2c_dly(); SCL_H(); i2c_dly();
    SDA_H(); i2c_dly();
}
static int i2c_tx_byte(uint8_t d) {
    for (int i = 0; i < 8; i++) {
        if (d & 0x80) SDA_H(); else SDA_L();
        d <<= 1; i2c_dly(); SCL_H(); i2c_dly(); SCL_L();
    }
    SDA_H(); i2c_dly(); SCL_H(); i2c_dly();
    int ack = SDA_R(); SCL_L();
    return ack;
}
static int i2c_write(const uint8_t *data, size_t len) {
    i2c_start();
    if (i2c_tx_byte(I2C_ADDR << 1)) { i2c_stop(); return -1; }
    for (size_t i = 0; i < len; i++)
        if (i2c_tx_byte(data[i])) { i2c_stop(); return -1; }
    i2c_stop();
    return 0;
}

static void fb_flush(void)
{
    /* 设置窗口 */
    uint8_t addr[] = {0x00, 0x21, 0x00, 0x7F, 0x22, 0x00, 0x07};
    if (i2c_write(addr, sizeof(addr)) < 0) { s_i2c_ok = 0; return; }
    /* 分页发送 (每次128字节, 共8页) */
    uint8_t tx[129];
    tx[0] = 0x40;
    for (int page = 0; page < 8; page++) {
        memcpy(tx + 1, s_fb + page * 128, 128);
        if (i2c_write(tx, 129) < 0) { s_i2c_ok = 0; return; }
    }
    s_i2c_ok = 1; /* 成功后恢复标记 */
}

/* ==================== 帧缓冲 ==================== */
static void fb_clr(void) { memset(s_fb, 0, sizeof(s_fb)); }
static void fb_fill_all(int c) { memset(s_fb, c ? 0xFF : 0x00, sizeof(s_fb)); }
static void fb_px(int x, int y, int c) {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    if (c) s_fb[x + (y / 8) * 128] |= (1 << (y % 8));
    else   s_fb[x + (y / 8) * 128] &= ~(1 << (y % 8));
}
static void fb_rect(int x, int y, int w, int h, int fill) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            fb_px(i, j, fill);
}
static void fb_ch(int x, int y, char ch, int color) {
    if (ch < 32 || ch > 126) ch = ' ';
    int idx = ch - 32;
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 8; j++)
            fb_px(x + i, y + j, (font[idx][i] >> j) & 1 ? color : !color);
}
static void fb_str(int x, int y, const char *s, int color) {
    while (*s) { fb_ch(x, y, *s, color); x += 6; s++; }
}
/* 大号数字 (12x16) */
static void fb_bignum(int x, int y, char ch, int c) {
    if (ch < '0' || ch > '9') return;
    int idx = ch - '0' + 16;
    for (int i = 0; i < 6; i++) for (int j = 0; j < 8; j++) {
        int p = (font[idx][i] >> j) & 1;
        for (int dx = 0; dx < 2; dx++) for (int dy = 0; dy < 2; dy++)
            fb_px(x + i * 2 + dx, y + j * 2 + dy, p ? c : !c);
    }
}
static void fb_bigstr(int x, int y, const char *s, int c) {
    while (*s) {
        if (*s >= '0' && *s <= '9') { fb_bignum(x, y, *s, c); x += 14; s++; }
        else if (*s == ',') { x += 7; s++; }
        else { x += 14; s++; }
    }
}
static int bigstr_w(const char *s) {
    int w = 0; while (*s) {
        if (*s >= '0' && *s <= '9') { w += 14; s++; }
        else if (*s == ',') { w += 7; s++; }
        else { w += 14; s++; }
    } return w;
}
static void fb_bigstr_inv(int x, int y, const char *s) {
    int w = bigstr_w(s); if (x < 0) x = 0; if (x + w > 128) w = 128 - x;
    fb_rect(x, y, w, 16, 1); fb_bigstr(x, y, s, 0);
}

/* ==================== UTC转北京时间 ==================== */
static void utc_to_bj(const char *utc, const char *date, char *out, size_t len)
{
    int h = 0, m = 0, s = 0, d = 0, mo = 0, y = 0;
    if (strlen(utc) >= 6) sscanf(utc, "%2d%2d%2d", &h, &m, &s);
    if (strlen(date) >= 6) sscanf(date, "%2d%2d%2d", &d, &mo, &y);
    y += 2000; h += 8;
    if (h >= 24) { h -= 24; d += 1; }
    int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) days[1] = 29;
    if (mo >= 1 && mo <= 12 && d > days[mo - 1]) { d = 1; mo++; }
    if (mo > 12) { mo = 1; y++; }
    snprintf(out, len, "%04d %02d %02d %02d:%02d:%02d", y, mo, d, h, m, s);
}

/* ==================== TCT商标绘制 ==================== */
static void draw_splash(void)
{
    fb_clr();
    int pad = 4;
    int B = 10;
    int tw = B * 3;
    int t1x = 14, cx = 54, t2x = 84, sy = pad;
    fb_rect(10, 0, 108, 52, 1);
    fb_rect(t1x, sy, tw, B, 0);
    fb_rect(t1x + B, sy + B, B, B*2, 0);
    fb_rect(cx + B, sy, B, B, 0);
    fb_rect(cx, sy + B, B, B, 0);
    fb_rect(cx + B, sy + B*2, B, B, 0);
    fb_rect(t2x, sy, tw, B, 0);
    fb_rect(t2x + B, sy + B, B, B*2, 0);
    fb_str(19, 40, "TCT GPS TRACKER", 0);
}

/* ==================== I2C初始化 ==================== */
esp_err_t oled_init(void)
{
    gpio_set_direction(PIN_SDA, GPIO_MODE_OUTPUT_OD);
    gpio_set_direction(PIN_SCL, GPIO_MODE_OUTPUT_OD);
    gpio_set_pull_mode(PIN_SDA, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_SCL, GPIO_PULLUP_ONLY);
    SDA_H(); SCL_H();
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t probe[] = {0x00, 0xAE};
    if (i2c_write(probe, 2) < 0) {
        ESP_LOGW(TAG, "未检测到OLED"); s_i2c_ok = 0; return ESP_OK;
    }
    ESP_LOGI(TAG, "检测到OLED(0x3C)");

    uint8_t cmds[][4] = {
        {0xAE},{0xD5,0x80},{0xA8,0x3F},{0xD3,0x00},{0x40},
        {0x8D,0x14},{0x20,0x00},{0xA1},{0xC8},{0xDA,0x12},
        {0x81,0xCF},{0xD9,0xF1},{0xDB,0x40},{0xA4},{0xA6},{0xAF}
    };
    /* 先发除0xAF外的所有初始化命令 */
    for (int i = 0; i < 15; i++) {
        uint8_t tx[8]; tx[0] = 0x00;
        memcpy(tx + 1, cmds[i], sizeof(cmds[i]));
        if (i2c_write(tx, sizeof(cmds[i]) + 1) < 0) { s_i2c_ok = 0; return ESP_OK; }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    /* 清屏后再开显示, 避免GDDRAM上电随机值显示乱码 */
    fb_clr(); fb_flush();
    uint8_t on[] = {0x00, 0xAF};
    i2c_write(on, 2);
    draw_splash();
    fb_flush();
    s_boot_ms = (uint32_t)(esp_timer_get_time() / 1000);
    ESP_LOGI(TAG, "OLED初始化完成");
    return ESP_OK;
}

/* ==================== 冷启动/正常运行(统一入口) ==================== */
void oled_coldstart(void)
{
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t elapsed = now_ms - s_boot_ms;
    int total_s = elapsed / 1000;

    if (elapsed < 30000) {
        /* 前30秒: TCT商标 + 冷启动倒计时 */
        draw_splash();
        int sec = 30 - total_s;
        if (sec < 0) sec = 0;
        char w[24];
        snprintf(w, sizeof(w), "GPS COLD START.. %2ds", sec);
        fb_str(0, 56, w, 1);
        fb_flush();
        return;
    }

    /* 30秒后: 黑底白字数据展示 */
    fb_clr();
    char buf[28];

    int sim_h = (total_s / 3600) % 24;
    int sim_m = (total_s / 60) % 60;
    int sim_s = total_s % 60;

    /* 第1行 y=0:  IP(动态STA/AP)    S/A  卫星数 */
    const char *sta_ip = wifi_manager_get_sta_ip();
    int sta_ok = wifi_manager_is_sta_connected() && sta_ip && strcmp(sta_ip, "0.0.0.0") != 0;
    if (sta_ok) {
        snprintf(buf, sizeof(buf), "%s", sta_ip);
        fb_str(0, 0, buf, 1);
        fb_str(98, 0, "S", 1);  /* STA模式 */
    } else {
        fb_str(0, 0, "192.168.4.1", 1);
        fb_str(98, 0, "A", 1);  /* AP模式 */
    }
    snprintf(buf, sizeof(buf), "%02d", (total_s % 32));
    fb_str(116, 0, buf, 1);

    /* 第2行 y=12: 北京时间 */
    snprintf(buf, sizeof(buf), "2026 05 11 %02d:%02d:%02d", sim_h, sim_m, sim_s);
    fb_str(0, 12, buf, 1);

    /* 第3-4行 y=24: 网格ID 放大居中 */
    fb_bigstr(15, 24, "999|999", 1);

    /* 第5行 y=44: 经纬度 */
    fb_str(0, 44, "N39.9042  E116.4074", 1);

    /* 第6行 y=56: 速度(节+km/h) 海拔 */
    fb_str(0, 56, "23kmh  50m", 1);

    fb_flush();
}

/* ==================== 统一更新接口 ==================== */
void oled_update(const gps_data_t *gps, const char *grid_id, const char *wifi_mode)
{
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t elapsed = now_ms - s_boot_ms;

    if (elapsed < 30000) {
        /* 前30秒: TCT商标 + 冷启动倒计时 */
        draw_splash();
        int sec = 30 - (int)(elapsed / 1000);
        if (sec < 0) sec = 0;
        char w[24];
        snprintf(w, sizeof(w), "GPS COLD START.. %2ds", sec);
        fb_str(0, 56, w, 1);
        fb_flush();
        return;
    }

    /* 30秒后: 真实GPS数据展示 */
    fb_clr();
    char buf[28];

    /* 第1行: WiFi IP(变长)        定位模式 卫星数(固定位) */
    const char *sta_ip = wifi_manager_get_sta_ip();
    int sta_ok = wifi_manager_is_sta_connected() && sta_ip && strcmp(sta_ip, "0.0.0.0") != 0;
    fb_str(0, 0, sta_ok ? "S" : "A", 1);
    fb_str(12, 0, sta_ok ? sta_ip : "192.168.4.1", 1);
    char gm = gps ? gps->mode : 0;
    if (!gm) gm = (gps && gps->status == 'A') ? 'A' : 'N';
    buf[0] = gm; buf[1] = 0;
    fb_str(102, 0, buf, 1);
    snprintf(buf, sizeof(buf), "%02d", gps ? gps->satellites : 0);
    fb_str(116, 0, buf, 1);

    /* 第2行 y=12: 年月日 时分秒 (秒数按运行时间平滑递增) */
    static int last_h=0,last_m=0,last_s=0,last_d=0,last_mo=0,last_y=0;
    static uint32_t last_tick=0;
    if (gps && strlen(gps->bj_time) > 0) {
        int h, m, s, d=0, mo=0, y=0;
        sscanf(gps->bj_time, "%2d%2d%2d", &h, &m, &s);
        if (strlen(gps->date) >= 6) sscanf(gps->date, "%2d%2d%2d", &d, &mo, &y);
        last_h=h;last_m=m;last_s=s;last_d=d;last_mo=mo;last_y=y;last_tick=now_ms;
    }
    if (last_tick) {
        int elapsed = (int)(now_ms - last_tick) / 1000;
        int s = last_s + elapsed;
        int m = last_m; int h = last_h;
        while (s >= 60) { s -= 60; m++; }
        while (m >= 60) { m -= 60; h++; }
        h %= 24;
        snprintf(buf, sizeof(buf), "%02d-%02d-%02d %02d:%02d:%02d", last_y+2000, last_mo, last_d, h, m, s);
    } else {
        snprintf(buf, sizeof(buf), "----/--/-- --:--:--");
    }
    fb_str(0, 12, buf, 1);

    /* 第3-4行 y=24: 网格ID 放大居中 */
    if (grid_id && strlen(grid_id) > 0 && strcmp(grid_id, "0") != 0) {
        fb_bigstr(15, 24, grid_id, 1);
    } else {
        fb_bigstr(15, 24, "---", 1);
    }

    /* 第5行 y=44: 经纬度 */
    if (gps && gps->status == 'A' && gps->latitude != 0) {
        char lat_c = gps->latitude >= 0 ? 'N' : 'S';
        char lng_c = gps->longitude >= 0 ? 'E' : 'W';
        snprintf(buf, sizeof(buf), "%.6f  %.6f", fabs(gps->latitude), fabs(gps->longitude));
    } else {
        snprintf(buf, sizeof(buf), "NO FIX");
    }
    fb_str(0, 44, buf, 1);

    /* 第6行 y=56: 速度 + 海拔 */
    if (gps && gps->status == 'A') {
        snprintf(buf, sizeof(buf), "%.2fkn %.2fkmh %.1fm", gps->speed_knots, gps->speed_kmh, gps->altitude);
    } else {
        snprintf(buf, sizeof(buf), "--kn --kmh --m");
    }
    fb_str(0, 56, buf, 1);

    fb_flush();
}
