/**
 * @file PowerRune_Events.h
 * @brief 大符事件库
 * @version 1.8
 * @date 2024-02-24
 */
#pragma once
#include "firmware.h"
#include "esp_event.h"
#include "espnow_protocol.h"

// PowerRune_Evets
ESP_EVENT_DEFINE_BASE(PRC);
ESP_EVENT_DEFINE_BASE(PRA);
ESP_EVENT_DEFINE_BASE(PRM);
#pragma pack(1)
// 事件循环Handle
extern esp_event_loop_handle_t pr_events_loop_handle;
// ADDRESS
// 5个ESP32S3[0:4], 1个ESP32C3[5]
// 公有事件
enum
{
    OTA_BEGIN_EVENT,
    OTA_COMPLETE_EVENT,
    CONFIG_EVENT,
    CONFIG_COMPLETE_EVENT,
    RESPONSE_EVENT,
    BEACON_TIMEOUT_EVENT,
};

struct OTA_BEGIN_EVENT_DATA
{
    uint8_t address = 0xFF;
    uint8_t data_len = sizeof(OTA_BEGIN_EVENT_DATA);
};
struct OTA_COMPLETE_EVENT_DATA
{
    uint8_t address = 0xFF;
    uint8_t data_len = sizeof(OTA_COMPLETE_EVENT_DATA);
    esp_err_t status;
    uint8_t ota_type; // 1 for startup ota, 0&2 for manual ota
};
struct CONFIG_EVENT_DATA // 比较浪费，但是省事
{
    uint8_t address = 0xFF;
    uint8_t data_len = sizeof(CONFIG_EVENT_DATA);
    PowerRune_Common_config_info_t config_common_info;
    PowerRune_Armour_config_info_t config_armour_info;
    PowerRune_Motor_config_info_t config_motor_info;
    PowerRune_Rlogo_config_info_t config_rlogo_info;
};
struct CONFIG_COMPLETE_EVENT_DATA
{
    uint8_t address = 0xFF;
    uint8_t data_len = sizeof(CONFIG_COMPLETE_EVENT_DATA);
    esp_err_t status;
};
struct RESPONSE_EVENT_DATA
{
    uint8_t address;
    uint8_t data_len = sizeof(RESPONSE_EVENT_DATA);
};
// Beacon Timeout不需要数据

// Armour事件
enum
{
    PRA_STOP_EVENT,
    PRA_START_EVENT,
    PRA_HIT_EVENT,
    PRA_COMPLETE_EVENT,
    PRA_PING_EVENT,
    // 2026: 主机向 5 块装甲板广播"已激活组数", 用于灯臂中部的 1/5、2/5 进度灯效。
    // 只能追加在末尾: 插在中间会让 START/STOP/HIT/COMPLETE 的编号整体错位。
    // 旧固件收到未知 id 会掉进 switch 的无匹配分支(什么都不做), 退化成"没有进度显示", 可以接受。
    PRA_PROGRESS_EVENT,
};

enum RUNE_MODE
{
    PRA_RUNE_BIG_MODE,
    PRA_RUNE_SMALL_MODE,
};

enum RUNE_COLOR
{
    PR_RED,
    PR_BLUE,
};

struct PRA_PING_EVENT_DATA
{
    uint8_t address;
    uint8_t data_len = sizeof(PRA_PING_EVENT_DATA);
    PowerRune_Armour_config_info_t config_info;
};

struct PRA_START_EVENT_DATA
{
    uint8_t address;
    uint8_t data_len = sizeof(PRA_START_EVENT_DATA);
    uint8_t mode = PRA_RUNE_BIG_MODE;
    uint8_t color = PR_RED;
};

// PRA_STOP_EVENT_DATA.color 的哨兵：表示"本次 STOP 不改颜色，保持当前"。
// STOP 有多处发送点（手动停止/收尾/OTA 复位），并非每处都该覆盖颜色。
#define PR_COLOR_KEEP 0xFF

struct PRA_STOP_EVENT_DATA
{
    uint8_t address;
    uint8_t data_len = sizeof(PRA_STOP_EVENT_DATA);
    // 第 0/1 字节被协议层占用(address/data_len)，有效字段从第 2 字节起。
    // 主控开符时用它把红蓝颜色同步给全部 5 块装甲板：主控只在"点亮某块"时
    // 随 START 下发颜色，没被点亮的符面会一直留着上一轮的颜色。
    uint8_t color = PR_COLOR_KEEP;
};

struct PRA_HIT_EVENT_DATA
{
    uint8_t address = 0xFF;
    uint8_t data_len = sizeof(PRA_HIT_EVENT_DATA);
    uint8_t score = 0;
};

struct PRA_COMPLETE_EVENT_DATA
{
    uint8_t address;
    uint8_t data_len = sizeof(PRA_COMPLETE_EVENT_DATA);
};

// 2026: 激活进度下发(主机 -> 各装甲板)
// 字段顺序有硬约束, 不可调换:
//   [0] address          —— 发送目标地址, 主机 tx_event_handler 用它选目的 MAC;
//                           从设备 parse_data_task 收到后会把本字节覆写成 0x06, 故不能放有意义的字段
//   [1] data_len         —— 主机 tx_event_handler 取它当发送长度
//   [2] activated_groups —— 已成功激活的组数 n
//   [3] total_groups     —— 总组数(5), 显式携带便于从设备算比例而不用硬编码
struct PRA_PROGRESS_EVENT_DATA
{
    uint8_t address = 0xFF;
    uint8_t data_len = sizeof(PRA_PROGRESS_EVENT_DATA);
    uint8_t activated_groups = 0;
    uint8_t total_groups = 5;
};

// 电机事件
enum
{
    PRM_UNLOCK_EVENT,
    PRM_UNLOCK_DONE_EVENT,
    PRM_START_EVENT,
    PRM_START_DONE_EVENT,
    PRM_SPEED_STABLE_EVENT,
    PRM_STOP_EVENT,
    PRM_DISCONNECT_EVENT,
    PRM_PING_EVENT,
};

enum PRM_DIRECTION
{
    PRM_DIRECTION_CLOCKWISE,     // 顺时针
    PRM_DIRECTION_ANTICLOCKWISE, // 逆时针
};

struct PRM_PING_EVENT_DATA
{
    uint8_t address = 0x05;
    uint8_t data_len = sizeof(PRM_PING_EVENT_DATA);
    PowerRune_Motor_config_info_t config_info;
};

struct PRM_UNLOCK_EVENT_DATA
{
    uint8_t address = 0x05;
    uint8_t data_len = sizeof(PRM_UNLOCK_EVENT_DATA);
};

struct PRM_UNLOCK_DONE_EVENT_DATA
{
    uint8_t address = 0x06;
    uint8_t data_len = sizeof(PRM_UNLOCK_DONE_EVENT_DATA);
    esp_err_t status;
};

struct PRM_START_EVENT_DATA
{
    uint8_t address = 0x05;
    uint8_t data_len = sizeof(PRM_START_EVENT_DATA);
    uint8_t mode = PRA_RUNE_BIG_MODE;
    uint8_t clockwise = PRM_DIRECTION_CLOCKWISE;
    float amplitude = 1.045;
    float omega = 1.884;
    float offset = 1.045;
};

struct PRM_START_DONE_EVENT_DATA
{
    uint8_t address = 0x06;
    uint8_t data_len = sizeof(PRM_START_DONE_EVENT_DATA);
    esp_err_t status;
    uint8_t mode;
};

struct PRM_SPEED_STABLE_EVENT_DATA
{
    uint8_t address = 0x06;
    uint8_t data_len = sizeof(PRM_SPEED_STABLE_EVENT_DATA);
};

struct PRM_STOP_EVENT_DATA
{
    uint8_t address = 0x05;
    uint8_t data_len = sizeof(PRM_STOP_EVENT_DATA);
};

struct PRM_DISCONNECT_EVENT_DATA
{
    uint8_t address = 0x06;
    uint8_t data_len = sizeof(PRM_DISCONNECT_EVENT_DATA);
};

#pragma pack()