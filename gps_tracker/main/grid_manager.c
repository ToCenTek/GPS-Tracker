#include "grid_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "grid_manager";
static param_grid_t s_grid = {0};
static bool s_has_grid = false;
static char s_json[96000];

#define MPD 111320.0
#define PI 3.141592653589793

static double deg2rad(double d) { return d * PI / 180.0; }

/* 经纬度→米 (以原点为基准) */
static void ll2m(double lat, double lng, double *x, double *y)
{
    double mc = cos(deg2rad(s_grid.origin_lat));
    *x = (lng - s_grid.origin_lng) * MPD * mc;
    *y = (lat - s_grid.origin_lat) * MPD;
}

/* 米→经纬度 */
static void m2ll(double x, double y, double *lat, double *lng)
{
    double mc = cos(deg2rad(s_grid.origin_lat));
    *lng = s_grid.origin_lng + x / (MPD * mc);
    *lat = s_grid.origin_lat + y / MPD;
}

esp_err_t grid_manager_init(void)
{
    esp_err_t ret = nvs_flash_init_partition("nvs_grid");
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase_partition("nvs_grid");
        ret = nvs_flash_init_partition("nvs_grid");
    }
    if (ret != ESP_OK) ESP_LOGE(TAG, "nvs_grid分区初始化失败");
    esp_err_t err = grid_manager_load();
    if (err == ESP_OK && s_has_grid)
        ESP_LOGI(TAG, "从NVS加载网格: %d行 x %d列", s_grid.total_rows, s_grid.total_cols);
    else
        ESP_LOGI(TAG, "无网格数据");
    return ESP_OK;
}

esp_err_t grid_manager_generate(double corners[4][2], double cell_size_m)
{
    /* 计算旋转角: 取第1-2条边的方向 */
    double dx = (corners[1][1] - corners[0][1]) * MPD * cos(deg2rad(corners[0][0]));
    double dy = (corners[1][0] - corners[0][0]) * MPD;
    s_grid.angle = atan2(dy, dx);
    s_grid.origin_lat = corners[0][0];
    s_grid.origin_lng = corners[0][1];
    s_grid.cell_size = cell_size_m;

    /* 把所有4个角转到局部坐标系 */
    double cx[4], cy[4];
    double ca = cos(s_grid.angle), sa = sin(s_grid.angle);
    for (int i = 0; i < 4; i++) {
        double mx, my;
        ll2m(corners[i][0], corners[i][1], &mx, &my);
        cx[i] = mx * ca + my * sa;
        cy[i] = -mx * sa + my * ca;
        memcpy(s_grid.polygon[i], corners[i], 2 * sizeof(double));
    }

    /* 计算局部bbox */
    double minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
    for (int i = 0; i < 4; i++) {
        if (cx[i] < minx) minx = cx[i];
        if (cx[i] > maxx) maxx = cx[i];
        if (cy[i] < miny) miny = cy[i];
        if (cy[i] > maxy) maxy = cy[i];
    }

    double w = maxx - minx, h = maxy - miny;
    s_grid.total_cols = (int)ceil(w / cell_size_m);
    s_grid.total_rows = (int)ceil(h / cell_size_m);
    if (s_grid.total_cols < 1) s_grid.total_cols = 1;
    if (s_grid.total_rows < 1) s_grid.total_rows = 1;

    s_has_grid = true;
    grid_manager_save();

    ESP_LOGI(TAG, "网格已生成: %d行 x %d列 = %d格, 边长%.1fm, 角%.1f°",
             s_grid.total_rows, s_grid.total_cols,
             s_grid.total_rows * s_grid.total_cols, cell_size_m,
             s_grid.angle * 180.0 / PI);
    return ESP_OK;
}

void grid_manager_query(double lat, double lng, char *id, size_t len)
{
    if (!s_has_grid) { strncpy(id, "0", len); return; }

    double mx, my;
    ll2m(lat, lng, &mx, &my);
    double ca = cos(s_grid.angle), sa = sin(s_grid.angle);
    double lx = mx * ca + my * sa;
    double ly = -mx * sa + my * ca;

    /* 计算局部bbox */
    double cx[4], cy[4];
    for (int i = 0; i < 4; i++) {
        double mxx, myy;
        ll2m(s_grid.polygon[i][0], s_grid.polygon[i][1], &mxx, &myy);
        cx[i] = mxx * ca + myy * sa;
        cy[i] = -mxx * sa + myy * ca;
    }
    double minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
    for (int i = 0; i < 4; i++) {
        if (cx[i] < minx) minx = cx[i];
        if (cx[i] > maxx) maxx = cx[i];
        if (cy[i] < miny) miny = cy[i];
        if (cy[i] > maxy) maxy = cy[i];
    }

    int col = (int)floor((lx - minx) / s_grid.cell_size);
    int row = (int)floor((ly - miny) / s_grid.cell_size);
    if (col < 0 || col >= s_grid.total_cols || row < 0 || row >= s_grid.total_rows) {
        strncpy(id, "0", len);
        return;
    }
    snprintf(id, len, "%d,%d", row + 1, col + 1);
}

/* 生成单个格子4个角的经纬度 */
static void cell_corners(int row, int col, double pts[4][2])
{
    double ca = cos(s_grid.angle), sa = sin(s_grid.angle);

    /* 计算局部bbox */
    double cx[4], cy[4];
    for (int i = 0; i < 4; i++) {
        double mx, my;
        ll2m(s_grid.polygon[i][0], s_grid.polygon[i][1], &mx, &my);
        cx[i] = mx * ca + my * sa;
        cy[i] = -mx * sa + my * ca;
    }
    double minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
    for (int i = 0; i < 4; i++) {
        if (cx[i] < minx) minx = cx[i];
        if (cx[i] > maxx) maxx = cx[i];
        if (cy[i] < miny) miny = cy[i];
        if (cy[i] > maxy) maxy = cy[i];
    }

    double s = s_grid.cell_size;
    double x0 = minx + col * s, y0 = miny + row * s;
    double x1 = x0 + s, y1 = y0 + s;
    /* 逆时针4角: 逆旋回WGS84 */
    double local[4][2] = {{x0,y0},{x1,y0},{x1,y1},{x0,y1}};
    for (int i = 0; i < 4; i++) {
        double rx = local[i][0] * ca - local[i][1] * sa;
        double ry = local[i][0] * sa + local[i][1] * ca;
        m2ll(rx, ry, &pts[i][0], &pts[i][1]);
    }
}

char *grid_manager_get_json(void)
{
    if (!s_has_grid) { strcpy(s_json, "{\"grids\":[]}"); return s_json; }
    size_t pos = 0;
    pos += snprintf(s_json + pos, sizeof(s_json) - pos,
        "{\"grid_size\":%.1f,\"angle\":%.3f,\"rows\":%d,\"cols\":%d,\"total\":%d,\"polygon\":[",
        s_grid.cell_size, s_grid.angle, s_grid.total_rows, s_grid.total_cols,
        s_grid.total_rows * s_grid.total_cols);
    for (int i = 0; i < 4; i++)
        pos += snprintf(s_json + pos, sizeof(s_json) - pos, "%s[%.6f,%.6f]",
            i ? "," : "", s_grid.polygon[i][1], s_grid.polygon[i][0]);
    pos += snprintf(s_json + pos, sizeof(s_json) - pos, "],\"grids\":[");
    int n = s_grid.total_rows * s_grid.total_cols;
    int show = n > MAX_GRID_DISPLAY ? MAX_GRID_DISPLAY : n;
    for (int i = 0, idx = 0; i < s_grid.total_rows && idx < show; i++) {
        for (int j = 0; j < s_grid.total_cols && idx < show; j++, idx++) {
            double pts[4][2];
            cell_corners(i, j, pts);
            pos += snprintf(s_json + pos, sizeof(s_json) - pos,
                "%s{\"id\":\"%d,%d\",\"bounds\":[[%.6f,%.6f],[%.6f,%.6f]]}",
                idx ? "," : "", i+1, j+1,
                pts[0][1], pts[0][0], pts[2][1], pts[2][0]);
        }
    }
    pos += snprintf(s_json + pos, sizeof(s_json) - pos, "]}");
    return s_json;
}

char *grid_manager_get_cells_json(void)
{
    return grid_manager_get_json();
}

esp_err_t grid_manager_clear(void)
{
    s_has_grid = false;
    memset(&s_grid, 0, sizeof(s_grid));
    nvs_handle_t h;
    if (nvs_open_from_partition("nvs_grid", "grid_data", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "网格已清除");
    return ESP_OK;
}

esp_err_t grid_manager_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition("nvs_grid", "grid_data", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_blob(h, "grid", &s_grid, sizeof(s_grid));
    nvs_set_u8(h, "has", s_has_grid ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

esp_err_t grid_manager_load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition("nvs_grid", "grid_data", NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t sz = sizeof(s_grid);
    err = nvs_get_blob(h, "grid", &s_grid, &sz);
    if (err == ESP_OK) {
        uint8_t f = 0;
        nvs_get_u8(h, "has", &f);
        s_has_grid = f != 0;
    }
    nvs_close(h);
    return ESP_OK;
}

bool grid_manager_has_grid(void)
{
    return s_has_grid;
}