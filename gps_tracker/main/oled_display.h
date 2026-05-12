#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "esp_err.h"
#include "gps_parser.h"

/* I2C引脚定义 */
#define OLED_SDA_PIN  GPIO_NUM_21
#define OLED_SCL_PIN  GPIO_NUM_22
#define OLED_I2C_PORT I2C_NUM_0
#define OLED_ADDR     0x3C

/* 初始化OLED */
esp_err_t oled_init(void);

/* 更新显示内容 */
void oled_update(const gps_data_t *gps, const char *grid_id, const char *wifi_mode);

/* 显示启动画面 */
void oled_splash(void);

#endif
