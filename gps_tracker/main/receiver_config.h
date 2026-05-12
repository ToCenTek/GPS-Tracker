#ifndef RECEIVER_CONFIG_H
#define RECEIVER_CONFIG_H

#include "esp_err.h"

/* NMEA校验和计算 */
unsigned char receiver_nmea_checksum(const char *str);

/* 发送配置指令 */
esp_err_t receiver_send_command(const char *cmd);

#endif
