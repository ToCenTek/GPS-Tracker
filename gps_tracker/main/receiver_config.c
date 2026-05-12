#include "receiver_config.h"
#include "gps_parser.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "receiver_config";

unsigned char receiver_nmea_checksum(const char *str)
{
    unsigned char ck = 0;
    for (const char *p = str; *p; p++) ck ^= (unsigned char)*p;
    return ck;
}

esp_err_t receiver_send_command(const char *cmd)
{
    /* 如果是完整NMEA语句（以$开头），直接发送 */
    if (cmd[0] == '$') {
        return gps_parser_send_command(cmd);
    }
    /* 否则按 action,param,value 格式处理 */
    ESP_LOGI(TAG, "发送接收机配置指令: %s", cmd);
    return gps_parser_send_command(cmd);
}
