#ifndef GPS_PARSER_H
#define GPS_PARSER_H

#include "esp_err.h"
#include <stdbool.h>

/* GPS数据结构体，字段顺序严格按RMC语句 */
typedef struct {
    char bj_time[12];           /* 北京时间 HHMMSS.SSS (UTC+8) */
    char status;                /* A=有效, V=无效 */
    double latitude;            /* 纬度 (十进制度) */
    char latitude_ns;           /* N/S */
    double longitude;           /* 经度 (十进制度) */
    char longitude_ew;          /* E/W */
    double speed_knots;         /* 对地速度 (节) */
    double course;              /* 对地航向 (度) */
    char date[8];               /* 日期 DDMMYY */
    double magnetic_variation;  /* 磁偏角 */
    char magnetic_variation_dir; /* E/W */
    char mode;                  /* A/D/E/M/S/N */
    /* 额外字段（非RMC） */
    double altitude;            /* 海拔 (米) */
    double hdop;                /* 水平精度因子 */
    double speed_kmh;           /* 速度 (km/h) */
    double speed_ms;            /* 速度 (m/s) */
    int satellites;             /* 卫星数量 */
    char grid[20];              /* 网格ID "row,col" */
} gps_data_t;

/* 原始NMEA语句 */
typedef struct {
    char raw_rmc[160];
    char raw_gga[160];
    char raw_gsa[160];
    char raw_gsv[160];
    char raw_vtg[160];
    char raw_zda[160];
} gps_raw_t;

/* 初始化GPS解析器（配置UART） */
esp_err_t gps_parser_init(int uart_num, int baud_rate, int tx_pin, int rx_pin);

/* 解析下一帧NMEA数据 */
bool gps_parser_parse(gps_data_t *data);

/* 发送NMEA指令到GPS模块 */
esp_err_t gps_parser_send_command(const char *cmd);

/* 设置GPS波特率 */
esp_err_t gps_parser_set_baud(int baud_idx);

/* 获取最新GPS数据（线程安全） */
void gps_parser_get_latest(gps_data_t *data);

/* 获取原始NMEA语句（线程安全） */
void gps_parser_get_raw(gps_raw_t *raw);

/* 设置网格ID */
void gps_parser_set_grid(const char *grid_id);

#endif
