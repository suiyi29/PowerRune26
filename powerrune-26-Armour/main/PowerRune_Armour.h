/**
 * @file PowerRune_Armour.h
 * @brief PowerRune_Armour类的头文件
 * @version 0.2
 * @date 2024-02-18
 * @note 本文件用于维护装甲板上的所有LED、按键以及它们的事件处理
 */
#pragma once
#ifndef __POWERRUNE_ARMOUR_H__
#define __POWERRUNE_ARMOUR_H__
#include "LED_Strip.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "DEMUX.h"
#include "driver/gpio.h"
#include "firmware.h"
#include "PowerRune_Events.h"
#include "espnow_protocol.h"

#define MATRIX_REFRESH_PERIOD 100
#define BLINK_DELAY 83
// 检修优化: 开机后视为"调试/检修窗口"的时长。窗口内不做命中上报, 且允许本地连击进检修。
// 原实现这个窗口是写死的 3000(3秒), 与"连击5次进检修"搭配时实际按不进去。
#define MAINT_WINDOW_MS 10000
// 检修优化: 检修模式无操作自动退回待机的超时, 防止 "进了检修忘了退" 导致全白常亮发热。
#define MAINT_TIMEOUT_MS (5 * 60 * 1000)

// GPIO定义
// 扳机IO: 1 2 4 5 6 7 10 12 8 9
const gpio_num_t TRIGGER_IO[] = {GPIO_NUM_1, GPIO_NUM_2, GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_10, GPIO_NUM_12, GPIO_NUM_8, GPIO_NUM_9};
const uint8_t TRIGGER_IO_TO_SCORE[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
const gpio_num_t DEMUX_IO[] = {GPIO_NUM_14, GPIO_NUM_21, GPIO_NUM_38};
const gpio_num_t DEMUX_IO_enable = GPIO_NUM_13;
const gpio_num_t STRIP_IO = GPIO_NUM_11;

// LED_Strip_Enum
enum LED_Strip_Enum
{
    LED_STRIP_MAIN_ARMOUR,
    LED_STRIP_UPPER,
    LED_STRIP_LOWER,
    LED_STRIP_ARM,
    LED_STRIP_MATRIX,
};
// LED_Strip_State_t
enum LED_Strip_State_t
{
    LED_STRIP_DEBUG,
    LED_STRIP_IDLE,
    LED_STRIP_TARGET,
    LED_STRIP_HIT,
    LED_STRIP_BLINK,
    LED_STRIP_MAINTENANCE, // 检修模式: 所有灯全亮
};

struct LED_Strip_FSM_t
{
    RUNE_COLOR color = PR_RED;
    RUNE_MODE mode = PRA_RUNE_BIG_MODE;
    LED_Strip_State_t LED_Strip_State = LED_STRIP_DEBUG;
    uint8_t score = 0; // 默认值为0
};

class PowerRune_Armour
{
private:
    // 装甲板图案数组
    // 靶状图案点灯序号，超级长，不要展开
    // 依据实际PCB(3SE_24_Power_Rune123)网表重排：主灯环DIN0共271颗，灯带从最外圈向内螺旋
    // LED0=最外圈(半径5609)，LED270=最内圈(半径884)；共9个同心环
    // 小符激活(未击打)时靶面应点亮的环：规则要求 2环、6环、9环全亮（1-based，环1=最外，环9=最内）
    // 映射到本板 0-based（环0=最外）：my ring1=[48..89]、my ring5=[210..229]、my ring8=[262..270]
    // 另加4条跨环指示标（对角径向亮线，从PCB网表提取的同角度LED）：
    //   斜1(~30°):  ring0=4  ring3=138 ring4=177 ring6=232 ring7=251 (ring1/5/8已在环中)
    //   斜2(~150°): ring0=20 ring2=109 ring3=151 ring4=189 ring6=238 ring7=255
    //   斜3(~210°): ring0=28 ring2=116 ring3=158 ring4=195 ring6=242 ring7=257
    //   斜4(~330°): ring0=44 ring3=171 ring4=207 ring6=248 ring7=261
    constexpr static const uint16_t target_pic[] = {
        // --- 规则要求的环：2环、6环、9环 ---
        48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
        60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71,
        72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83,
        84, 85, 86, 87, 88, 89, 210, 211, 212, 213, 214, 215,
        216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227,
        228, 229, 262, 263, 264, 265, 266, 267, 268, 269, 270,
        // --- 4条跨环指示标（丝印LED编号→链索引，全部在主环DIN0链上）---
        // 图1: 31,11,21,113,445,150,446,186,230,244
        35, 36, 37, 123, 124, 164, 165, 201, 245, 259,
        // 图2: 438,257,256,443,93,448,132,168,220,238
        13, 12, 11, 102, 101, 145, 144, 183, 235, 253,
        // 图3: 439,440,441,103,431,447,141,177,225,241
        23, 24, 25, 112, 113, 154, 155, 192, 240, 256,
        // 图4: 2,1,442,444,83,449,123,159,215,235
        1, 0, 47, 91, 90, 135, 134, 174, 230, 250,
    };
    // 命中图案，各环截止点[环首LED]
    // 实际PCB共9环，环0=最外圈(48颗) ... 环8=最内圈(9颗)
    constexpr static const uint16_t hit_ring_cutoff[] = {0, 48, 90, 134, 174, 210, 230, 250, 262, 271};
    // 流水灯数组图案
    constexpr static bool single_arrow[] = {
        0,
        0,
        1,
        0,
        0,
        0,
        1,
        1,
        1,
        0,
        1,
        1,
        0,
        1,
        1,
        1,
        0,
        0,
        0,
        1,
        0,
        0,
        0,
        0,
        0,
    };

    // LED_Strip_FSM_t
    static LED_Strip_FSM_t state;
    // 2026 大符激活进度, 由主机 PRA_PROGRESS_EVENT 下发
    // 用单字节 volatile 而不是塞进 LED_Strip_FSM_t: S3 双核下 state_task = state 的
    // 结构体整体拷贝可能拿到不一致快照, 单字节读写天然原子。
    static volatile uint8_t activation_progress; // 已激活组数
    static volatile uint8_t activation_total;    // 总组数; 0 = 不显示进度
    // LED_Strip初始化
    static LED_Strip *led_strip[5];
    // DEMUX初始化
    static DEMUX demux_led;
    // ISR Mutex
    static SemaphoreHandle_t ISR_mutex;
    // LED更新任务
    static void LED_update_task(void *pvParameter);
    static void restart_ISR_task(void *pvParameter);
    // LED更新任务句柄
    static TaskHandle_t LED_update_task_handle;
    // 状态机更新信号量
    static SemaphoreHandle_t LED_Strip_FSM_Semaphore;
    // GPIO初始化
    static inline void GPIO_init();
    // GPIO中断启动
    static inline void GPIO_ISR_enable();
    // GPIO ISR处理
    static void IRAM_ATTR GPIO_ISR_handler(void *arg);
    // 装甲板启动，含红蓝方和大小符
    static inline void trigger(RUNE_MODE mode, RUNE_COLOR color);
    // 装甲板命中
    static inline void hit(uint8_t score);
    // 装甲板清除
    static inline void clear_armour(bool refresh = true);
    // 装甲板激活完毕
    static inline void blink();
    // 装甲板DEBUG
    static inline void debug();
    // 装甲板停止
    static inline void stop();
    // 检修模式: 所有灯全亮
    static inline void enter_maintenance();
    // 2026: 灯臂灯条显示激活进度(n/总) + 前端流动箭头
    static inline void show_arm_progress(const PowerRune_Armour_config_info_t *config_info, RUNE_COLOR color);
    // GPIO轮询&滤波服务
    static void GPIO_polling_service(void *pvParameter);

public:
    PowerRune_Armour();
    static void global_pr_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data);
};

#endif