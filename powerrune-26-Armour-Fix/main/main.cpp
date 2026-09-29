/**
 * @file main_test_all_on.cpp
 * @brief 硬件测试：靶面(MAIN_ARMOUR) + 上装甲(UPPER) + 下装甲(LOWER) + 灯臂(ARM) + 矩阵(MATRIX) 全部灯珠全亮
 *
 * 使用方法：
 *   1. 备份原 main.cpp（重命名为 main_orig.cpp）
 *   2. 将本文件重命名为 main.cpp
 *   3. 编译烧录
 *   4. 测试完成后恢复原 main.cpp
 *
 * 硬件连接：
 *   - 5条WS2812B灯带通过74HC4051 DEMUX复用GPIO11
 *   - DEMUX地址线: GPIO14(A), GPIO21(B), GPIO38(C)
 *   - DEMUX使能: GPIO13 (低电平有效)
 *   - 通道0=MAIN_ARMOUR(271颗), 1=UPPER(86), 2=LOWER(92), 3=ARM(53), 4=MATRIX(165)
 */
#include "PowerRune_Armour.h" // 已包含 LED_Strip.h 和 DEMUX.h，以及 GPIO 定义

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "TEST_ALL_ON";

// 灯带长度配置（与实际PCB一致）
static const uint16_t STRIP_LENGTHS[5] = {
    271, // LED_STRIP_MAIN_ARMOUR: 主靶面环 (9环, 从外向内螺旋)
    86,  // LED_STRIP_UPPER:      上装甲板
    92,  // LED_STRIP_LOWER:      下装甲板
    53,  // LED_STRIP_ARM:        灯臂 (12+10+9+10+12)
    165, // LED_STRIP_MATRIX:     矩阵灯
};

static const char *STRIP_NAMES[5] = {
    "MAIN_ARMOUR", "UPPER", "LOWER", "ARM", "MATRIX"
};

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== All-ON Hardware Test ===");
    ESP_LOGI(TAG, "Total LEDs: %d", STRIP_LENGTHS[0] + STRIP_LENGTHS[1] + STRIP_LENGTHS[2] + STRIP_LENGTHS[3] + STRIP_LENGTHS[4]);

    // 初始化DEMUX (74HC4051, 低电平使能)
    DEMUX demux(DEMUX_IO, DEMUX_IO_enable);
    ESP_LOGI(TAG, "DEMUX initialized (addr: GPIO14/GPIO21/GPIO38, en: GPIO13)");

    // 初始化5条灯带（共用GPIO11，通过DEMUX切换）
    LED_Strip *strip[5];
    for (int i = 0; i < 5; i++)
    {
        strip[i] = new LED_Strip(STRIP_IO, STRIP_LENGTHS[i]);
        ESP_LOGI(TAG, "Strip %d [%s]: %d LEDs", i, STRIP_NAMES[i], STRIP_LENGTHS[i]);
    }

    // 亮度 (0~255, WS2812B建议不超过100以避免电流过大)
    const uint8_t BRIGHTNESS = 80;

    ESP_LOGI(TAG, "Turning ON all LEDs (white, brightness=%d)...", BRIGHTNESS);

    // 循环刷新所有灯带（WS2812B锁存后保持，但循环刷新防止干扰）
    uint32_t loop_count = 0;
    while (1)
    {
        for (int i = 0; i < 5; i++)
        {
            demux = i;                        // 选择DEMUX通道
            strip[i]->set_color(BRIGHTNESS, BRIGHTNESS, BRIGHTNESS); // 全亮白色
            strip[i]->refresh();              // 发送数据
        }
        loop_count++;
        if (loop_count % 100 == 0)
        {
            ESP_LOGI(TAG, "Refresh loop %lu, all strips ON", loop_count);
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // 20Hz刷新
    }
}
