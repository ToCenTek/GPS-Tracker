#ifndef GRID_MANAGER_H
#define GRID_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

#define MAX_POLYGON_POINTS 100
#define MAX_GRID_CELLS 500

/* 网格单元 */
typedef struct {
    int row;
    int col;
    double min_lat;
    double min_lng;
    double max_lat;
    double max_lng;
} grid_cell_t;

/* 网格数据 */
typedef struct {
    int total_rows;
    int total_cols;
    int total_cells;
    double grid_size;       /* 边长（米） */
    double min_lat, min_lng;
    double max_lat, max_lng;
    int point_count;
    double polygon[MAX_POLYGON_POINTS][2];
    grid_cell_t cells[MAX_GRID_CELLS];
} grid_data_t;

/* 初始化网格管理器（NVS） */
esp_err_t grid_manager_init(void);

/* 设置区域多边形 */
esp_err_t grid_manager_set_polygon(double points[][2], int count);

/* 生成网格 */
esp_err_t grid_manager_generate(double grid_size_meters);

/* 查询某点所在的网格ID，返回 "row,col" 或 "0" */
void grid_manager_query(double lat, double lng, char *grid_id, size_t len);

/* 获取网格数据JSON（含区域多边形） */
char *grid_manager_get_json(void);

/* 获取网格JSON（仅格子，不含多边形） */
char *grid_manager_get_cells_json(void);

/* 清除网格 */
esp_err_t grid_manager_clear(void);

/* 保存网格到NVS */
esp_err_t grid_manager_save(void);

/* 从NVS加载网格 */
esp_err_t grid_manager_load(void);

/* 是否有网格 */
bool grid_manager_has_grid(void);

#endif
