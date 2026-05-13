#ifndef GRID_MANAGER_H
#define GRID_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

#define MAX_GRID_DISPLAY 500  /* JSON返回最多500格用于显示 */

/* 参数化网格: 不存格子, 查询用公式算 */
typedef struct {
    double origin_lat, origin_lng;   /* 原点 (左下角, WGS84) */
    double angle;                    /* 旋转角 (弧度), 0=正北 */
    double cell_size;                /* 边长(米) */
    int total_rows, total_cols;      /* 行列数 */
    double polygon[4][2];            /* 4个角 [lat,lng] */
} param_grid_t;

esp_err_t grid_manager_init(void);

/* 生成网格: 从4个角点和步长计算参数 */
esp_err_t grid_manager_generate(double corners[4][2], double cell_size_m);

/* 查询点在哪格, 返回 "row,col" 或 "0" */
void grid_manager_query(double lat, double lng, char *id, size_t len);

/* 获取网格JSON (含区域 + 前MAX_GRID_DISPLAY格的坐标) */
char *grid_manager_get_json(void);

/* 仅格子JSON */
char *grid_manager_get_cells_json(void);

/* 清除 */
esp_err_t grid_manager_clear(void);

/* NVS持久化 */
esp_err_t grid_manager_save(void);
esp_err_t grid_manager_load(void);
bool grid_manager_has_grid(void);

#endif