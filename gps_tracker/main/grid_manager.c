#include "grid_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "grid_manager";
static grid_data_t s_grid = {0};
static bool s_has_grid = false;

/* 地球参数 */
#define EARTH_RADIUS 6371000.0
#define METER_PER_DEG 111320.0

/* 经纬度转米 */
static void latlng_to_meters(double lat, double lng, double *x, double *y)
{
    double mid_lat = (s_grid.min_lat + s_grid.max_lat) / 2.0 * M_PI / 180.0;
    *x = (lng - s_grid.min_lng) * METER_PER_DEG * cos(mid_lat);
    *y = (lat - s_grid.min_lat) * METER_PER_DEG;
}

/* 米转经纬度 */
static void meters_to_latlng(double x, double y, double *lat, double *lng)
{
    double mid_lat = (s_grid.min_lat + s_grid.max_lat) / 2.0 * M_PI / 180.0;
    *lng = s_grid.min_lng + x / (METER_PER_DEG * cos(mid_lat));
    *lat = s_grid.min_lat + y / METER_PER_DEG;
}

esp_err_t grid_manager_init(void)
{
    /* 尝试从NVS加载 */
    esp_err_t err = grid_manager_load();
    if (err == ESP_OK && s_has_grid) {
        ESP_LOGI(TAG, "从NVS加载网格: %d行 x %d列 = %d个",
                 s_grid.total_rows, s_grid.total_cols, s_grid.total_cells);
    } else {
        ESP_LOGI(TAG, "网格管理器初始化完成 (无网格数据)");
    }
    return ESP_OK;
}

esp_err_t grid_manager_set_polygon(double points[][2], int count)
{
    if (count < 3 || count > MAX_POLYGON_POINTS) return ESP_ERR_INVALID_ARG;
    s_grid.point_count = count;
    s_grid.min_lat = 1e9; s_grid.min_lng = 1e9;
    s_grid.max_lat = -1e9; s_grid.max_lng = -1e9;
    for (int i = 0; i < count; i++) {
        s_grid.polygon[i][0] = points[i][0];
        s_grid.polygon[i][1] = points[i][1];
        if (points[i][0] < s_grid.min_lat) s_grid.min_lat = points[i][0];
        if (points[i][0] > s_grid.max_lat) s_grid.max_lat = points[i][0];
        if (points[i][1] < s_grid.min_lng) s_grid.min_lng = points[i][1];
        if (points[i][1] > s_grid.max_lng) s_grid.max_lng = points[i][1];
    }
    return ESP_OK;
}

esp_err_t grid_manager_generate(double grid_size_meters)
{
    double w_m, h_m;
    latlng_to_meters(s_grid.max_lat, s_grid.max_lng, &w_m, &h_m);
    /* w_m 和 h_m 是从min_lat/min_lng起的偏移 */
    double mid_lat = (s_grid.min_lat + s_grid.max_lat) / 2.0 * M_PI / 180.0;
    double w = (s_grid.max_lng - s_grid.min_lng) * METER_PER_DEG * cos(mid_lat);
    double h = (s_grid.max_lat - s_grid.min_lat) * METER_PER_DEG;

    s_grid.grid_size = grid_size_meters;
    s_grid.total_cols = (int)ceil(w / grid_size_meters);
    s_grid.total_rows = (int)ceil(h / grid_size_meters);
    s_grid.total_cells = 0;

    double lng_step = (s_grid.max_lng - s_grid.min_lng) / s_grid.total_cols;
    double lat_step = (s_grid.max_lat - s_grid.min_lat) / s_grid.total_rows;

    for (int r = 0; r < s_grid.total_rows && s_grid.total_cells < MAX_GRID_CELLS; r++) {
        for (int c = 0; c < s_grid.total_cols && s_grid.total_cells < MAX_GRID_CELLS; c++) {
            grid_cell_t *cell = &s_grid.cells[s_grid.total_cells];
            cell->row = r + 1;
            cell->col = c + 1;
            cell->min_lat = s_grid.min_lat + r * lat_step;
            cell->min_lng = s_grid.min_lng + c * lng_step;
            cell->max_lat = cell->min_lat + lat_step;
            cell->max_lng = cell->min_lng + lng_step;
            s_grid.total_cells++;
        }
    }

    s_has_grid = s_grid.total_cells > 0;
    grid_manager_save();

    ESP_LOGI(TAG, "网格已生成: %d行 x %d列 = %d个 (边长%.1fm)",
             s_grid.total_rows, s_grid.total_cols, s_grid.total_cells, grid_size_meters);
    return ESP_OK;
}

void grid_manager_query(double lat, double lng, char *grid_id, size_t len)
{
    if (!s_has_grid) {
        strncpy(grid_id, "0", len);
        return;
    }
    if (lat < s_grid.min_lat || lat > s_grid.max_lat ||
        lng < s_grid.min_lng || lng > s_grid.max_lng) {
        strncpy(grid_id, "0", len);
        return;
    }
    double mid_lat = (s_grid.min_lat + s_grid.max_lat) / 2.0 * M_PI / 180.0;
    double dx = (lng - s_grid.min_lng) * METER_PER_DEG * cos(mid_lat);
    double dy = (lat - s_grid.min_lat) * METER_PER_DEG;
    int col = (int)floor(dx / s_grid.grid_size) + 1;
    int row = (int)floor(dy / s_grid.grid_size) + 1;
    if (col < 1 || col > s_grid.total_cols || row < 1 || row > s_grid.total_rows) {
        strncpy(grid_id, "0", len);
        return;
    }
    snprintf(grid_id, len, "%d,%d", row, col);
}

char *grid_manager_get_json(void)
{
    static char json[16384];
    size_t pos = 0;
    pos += snprintf(json + pos, sizeof(json) - pos,
        "{\"polygon\":[");
    for (int i = 0; i < s_grid.point_count; i++) {
        pos += snprintf(json + pos, sizeof(json) - pos, "%s[%.6f,%.6f]",
            i > 0 ? "," : "", s_grid.polygon[i][1], s_grid.polygon[i][0]);
    }
    pos += snprintf(json + pos, sizeof(json) - pos,
        "],\"grid_size\":%.1f,\"total_rows\":%d,\"total_cols\":%d,\"total_grids\":%d,\"grids\":[",
        s_grid.grid_size, s_grid.total_rows, s_grid.total_cols, s_grid.total_cells);
    int show = s_grid.total_cells > 500 ? 500 : s_grid.total_cells;
    for (int i = 0; i < show; i++) {
        grid_cell_t *c = &s_grid.cells[i];
        pos += snprintf(json + pos, sizeof(json) - pos,
            "%s{\"id\":\"%d,%d\",\"bounds\":[[%.6f,%.6f],[%.6f,%.6f]]}",
            i > 0 ? "," : "", c->row, c->col,
            c->min_lng, c->min_lat, c->max_lng, c->max_lat);
    }
    pos += snprintf(json + pos, sizeof(json) - pos, "]}");
    return json;
}

char *grid_manager_get_cells_json(void)
{
    static char json[16384];
    size_t pos = 0;
    pos += snprintf(json + pos, sizeof(json) - pos,
        "{\"grid_size\":%.1f,\"total_rows\":%d,\"total_cols\":%d,\"total_grids\":%d,\"grids\":[",
        s_grid.grid_size, s_grid.total_rows, s_grid.total_cols, s_grid.total_cells);
    int show = s_grid.total_cells > 500 ? 500 : s_grid.total_cells;
    for (int i = 0; i < show; i++) {
        grid_cell_t *c = &s_grid.cells[i];
        pos += snprintf(json + pos, sizeof(json) - pos,
            "%s{\"id\":\"%d,%d\",\"bounds\":[[%.6f,%.6f],[%.6f,%.6f]]}",
            i > 0 ? "," : "", c->row, c->col,
            c->min_lng, c->min_lat, c->max_lng, c->max_lat);
    }
    pos += snprintf(json + pos, sizeof(json) - pos, "]}");
    return json;
}

esp_err_t grid_manager_clear(void)
{
    s_has_grid = false;
    s_grid.total_cells = 0;
    s_grid.total_rows = 0;
    s_grid.total_cols = 0;
    s_grid.point_count = 0;
    /* 从NVS清除 */
    nvs_handle_t handle;
    esp_err_t err = nvs_open("grid_data", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "网格已清除");
    return ESP_OK;
}

esp_err_t grid_manager_save(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("grid_data", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    nvs_set_blob(handle, "grid_data", &s_grid, sizeof(grid_data_t));
    nvs_set_u8(handle, "has_grid", s_has_grid ? 1 : 0);
    nvs_commit(handle);
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t grid_manager_load(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("grid_data", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    size_t size = sizeof(grid_data_t);
    err = nvs_get_blob(handle, "grid_data", &s_grid, &size);
    if (err == ESP_OK) {
        uint8_t flag = 0;
        nvs_get_u8(handle, "has_grid", &flag);
        s_has_grid = flag != 0;
    }
    nvs_close(handle);
    return ESP_OK;
}

bool grid_manager_has_grid(void)
{
    return s_has_grid;
}
