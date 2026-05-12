#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "soc/gpio_num.h"
#include "hal/uart_types.h"
#include "wifi_manager.h"
#include "gps_parser.h"
#include "grid_manager.h"
#include "http_server.h"
#include "oled_display.h"

static const char *TAG = "gps_main";

/* OLED刷新任务 */
static void oled_task(void *arg)
{
    int counter = 0;
    while (1) {
        gps_data_t gps;
        gps_parser_get_latest(&gps);

        char grid_id[20] = "0";
        if (gps.status == 'A' && gps.latitude != 0) {
            grid_manager_query(gps.latitude, gps.longitude, grid_id, sizeof(grid_id));
            gps_parser_set_grid(grid_id);
        }

        const char *wifi_mode = "AP+STA";
        if (wifi_manager_is_sta_connected()) {
            wifi_mode = "AP+STA+INET";
        }

        ESP_LOGI("oled_task", "loop #%d", counter);
        counter++;

        oled_update(&gps, grid_id, wifi_mode);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    /* 初始化NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS初始化完成");

    /* 初始化OLED (I2C0: SDA=21, SCL=22) */
    ESP_ERROR_CHECK(oled_init());
    ESP_LOGI(TAG, "OLED初始化完成");

    /* 初始化WiFi (AP+STA) */
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_LOGI(TAG, "WiFi初始化完成, SSID: %s", WIFI_AP_SSID);

    /* 初始化GPS解析器 */
    ESP_ERROR_CHECK(gps_parser_init(
        UART_NUM_1,    /* 使用UART1连接GPS */
        115200,        /* 默认波特率115200 */
        GPIO_NUM_17,   /* TX (ESP32 -> GPS RX) */
        GPIO_NUM_16    /* RX (ESP32 <- GPS TX) */
    ));

    /* 初始化网格管理器 */
    ESP_ERROR_CHECK(grid_manager_init());

    /* 启动HTTP服务器 */
    ESP_ERROR_CHECK(http_server_init());

    /* 创建OLED刷新任务 */
    xTaskCreatePinnedToCore(oled_task, "oled", 8192, NULL, 5, NULL, 0);
    ESP_LOGI(TAG, "OLED刷新任务已创建");

    ESP_LOGI(TAG, "系统启动完成");
    ESP_LOGI(TAG, "AP模式: SSID=%s, 密码=%s", WIFI_AP_SSID, WIFI_AP_PASSWORD);
    ESP_LOGI(TAG, "访问 http://192.168.4.1 使用Web界面");
}
