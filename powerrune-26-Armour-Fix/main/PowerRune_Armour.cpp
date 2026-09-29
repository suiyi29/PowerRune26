/**
 * @file PowerRune_Armour.cpp
 * @brief 装甲板类
 * @version 0.2
 * @date 2024-02-19
 * @note 装甲板类，用于装甲板控制
 */
#include "PowerRune_Armour.h"
static const char *TAG_ARMOUR = "Armour";
// 变量初始化
LED_Strip *PowerRune_Armour::led_strip[5];
SemaphoreHandle_t PowerRune_Armour::ISR_mutex = xSemaphoreCreateBinary();
DEMUX PowerRune_Armour::demux_led = DEMUX(DEMUX_IO, DEMUX_IO_enable);
TaskHandle_t PowerRune_Armour::LED_update_task_handle;
SemaphoreHandle_t PowerRune_Armour::LED_Strip_FSM_Semaphore;
LED_Strip_FSM_t PowerRune_Armour::state;

// 2026 大符激活进度(由主机 PRA_PROGRESS_EVENT 下发)
volatile uint8_t PowerRune_Armour::activation_progress = 0;
volatile uint8_t PowerRune_Armour::activation_total = 0;

bool valid[10] = {true, true, true, true, true, true, true, true, true, true};

void PowerRune_Armour::clear_armour(bool refresh)
{
    for (uint8_t i = 0; i < 5; i++)
    {
        demux_led = i;
        led_strip[i]->clear_pixels();
        if (refresh)
            led_strip[i]->refresh();
    }
}

// 2026: 灯臂中部进度灯效 —— 点亮前 n/总 段, 未满时在前沿再点一颗充当流动箭头。
// 规则原文(§5.5.2): "大能量机关的灯臂中部灯效将指示激活进度……中部灯臂将亮起约 1/5"。
// ⚠ 本函数只允许在 LED_update_task 里调用(它动 demux_led 和唯一的 RMT 通道)。
void PowerRune_Armour::show_arm_progress(const PowerRune_Armour_config_info_t *config_info, RUNE_COLOR color)
{
    uint8_t done = activation_progress;
    if (done > 5)
        done = 5;

    const uint8_t r = color == PR_RED ? config_info->brightness_proportion_edge : 0;
    const uint8_t b = color == PR_RED ? 0 : config_info->brightness_proportion_edge;

    // === 灯臂外围(ARM): 53颗, 从中间(索引26)向两端扩展 ===
    const uint16_t arm_len = 53; // 12+10+9+10+12=53
    const uint16_t arm_counts[6] = {0, 17, 27, 35, 45, 53};
    uint16_t arm_lit = arm_counts[done];

    LED_Strip *arm = led_strip[LED_STRIP_ARM];
    demux_led = LED_STRIP_ARM;
    arm->clear_pixels();
    if (arm_lit > 0)
    {
        uint16_t half = (arm_lit - 1) / 2;
        uint16_t start = (half > 26) ? 0 : (26 - half);
        uint16_t end = 26 + half;
        if (end >= arm_len)
            end = arm_len - 1;
        for (uint16_t j = start; j <= end; j++)
            arm->set_color_index(j, r, 0, b);
    }
    arm->refresh();

    // === 灯臂中心矩阵(MATRIX): 5行x33列=165颗, 索引0靠近靶面 ===
    // 从远离靶面的一端(最后几排)向靶面扩展
    // 第1组11排(55颗,索引110~164), 第2组17排(85颗,80~164), 第3组23排(115颗,50~164), 第4组28排(140颗,25~164), 第5组33排(全部)
    const uint16_t matrix_rows[6] = {0, 11, 17, 23, 28, 33};
    uint16_t rows = matrix_rows[done];
    uint16_t matrix_lit = rows * 5; // 每排5颗

    LED_Strip *matrix = led_strip[LED_STRIP_MATRIX];
    demux_led = LED_STRIP_MATRIX;
    matrix->clear_pixels();
    if (matrix_lit > 0)
    {
        uint16_t start = 165 - matrix_lit; // 从远离靶面端开始
        for (uint16_t j = start; j < 165; j++)
            matrix->set_color_index(j, r, 0, b);
    }
    matrix->refresh();
}

// LED更新任务
void PowerRune_Armour::LED_update_task(void *pvParameter)
{
    // 状态
    LED_Strip_FSM_t state_task;
    const PowerRune_Armour_config_info_t *config_info;

    // 清除所有灯效
    for (uint8_t i = 0; i < 5; i++)
    {
        demux_led = i;
        led_strip[i]->refresh();
    }
    while (1)
    {
        // 状态机，状态转移：START->IDLE->TARGET->HIT->BLINK->IDLE，TARGET->HIT和BLINK->IDLE过程之后task阻塞，等待信号量
        switch (state.LED_Strip_State)
        {
        case LED_STRIP_DEBUG:
        {
            do
            { // 初始化状态，使用valid变量显示损坏的装甲板
                demux_led = LED_STRIP_MAIN_ARMOUR;
                // led_strip[LED_STRIP_MAIN_ARMOUR]->clear_pixels();
                for (size_t i = 0; i < 9; i++)
                {
                    if (!valid[i])
                        for (uint16_t j = hit_ring_cutoff[i]; j < hit_ring_cutoff[i + 1]; j++)
                            led_strip[LED_STRIP_MAIN_ARMOUR]->set_color_index(j, 0, 100, 0); // Green
                    led_strip[LED_STRIP_MAIN_ARMOUR]->refresh();
                }
            } while (xSemaphoreTake(LED_Strip_FSM_Semaphore, 0) == pdFALSE);
            // 转移状态
            state_task = state;
            break;
        }
        case LED_STRIP_IDLE:
            clear_armour();
            // 2026: 非激活状态保持灯臂进度条(从中间向两端), 正在激活(TARGET)和闪烁(BLINK)时不显示
            config_info = config->get_config_info_pt();
            demux_led = LED_STRIP_ARM;
            if (state.mode == PRA_RUNE_BIG_MODE && activation_total > 0)
                show_arm_progress(config_info, state.color);
            else
                led_strip[LED_STRIP_ARM]->refresh();
            // 等待信号量
            xSemaphoreTake(LED_Strip_FSM_Semaphore, portMAX_DELAY);
            // 转移状态
            state_task = state;
            break;
        case LED_STRIP_TARGET:
        {
            clear_armour(false);
            config_info = config->get_config_info_pt();

            // 点亮靶状图案、上下装甲板，灯臂刷新一次
            demux_led = LED_STRIP_MAIN_ARMOUR;
            if (state_task.color == PR_RED)
            {
                for (uint16_t i = 0; i < sizeof(target_pic) / sizeof(uint16_t); i++)
                {
                    led_strip[LED_STRIP_MAIN_ARMOUR]->set_color_index(target_pic[i], config_info->brightness, 0, 0);
                }
            }
            else
            {
                for (uint16_t i = 0; i < sizeof(target_pic) / sizeof(uint16_t); i++)
                {
                    led_strip[LED_STRIP_MAIN_ARMOUR]->set_color_index(target_pic[i], 0, 0, config_info->brightness);
                }
            }
            led_strip[LED_STRIP_MAIN_ARMOUR]->refresh();
            /* 以下字段用于2024版本大能量机关程序，但是在2025版本中因灯效改变而需要注释掉
            demux_led = LED_STRIP_UPPER;
            led_strip[LED_STRIP_UPPER]->set_color(state_task.color == PR_RED ? config_info->brightness : 0, 0, state_task.color == PR_RED ? 0 : config_info->brightness);
            led_strip[LED_STRIP_UPPER]->refresh();
            demux_led = LED_STRIP_LOWER;
            led_strip[LED_STRIP_LOWER]->set_color(state_task.color == PR_RED ? config_info->brightness : 0, 0, state_task.color == PR_RED ? 0 : config_info->brightness);
            led_strip[LED_STRIP_LOWER]->refresh();
            */
            // 2026: 正在激活(TARGET)时灯臂不显示进度条, 由靶面图案+矩阵流水灯+灯臂箭头等灯效占据
            demux_led = LED_STRIP_ARM;
            led_strip[LED_STRIP_ARM]->refresh();
            // 开启矩阵流水灯
            uint8_t i = 0;
            do
            {
                TickType_t xLastWakeTime = xTaskGetTickCount();

                demux_led = LED_STRIP_MATRIX;

                if (state_task.color == PR_RED)
                    for (uint16_t j = 0; j < 165; j++)
                    {
                        led_strip[LED_STRIP_MATRIX]->set_color_index(j, single_arrow[(j + i * 5) % 25] * config_info->brightness, 0, 0);
                    }
                else
                    for (uint16_t j = 0; j < 165; j++)
                    {
                        led_strip[LED_STRIP_MATRIX]->set_color_index(j, 0, 0, single_arrow[(j + i * 5) % 25] * config_info->brightness);
                    }
                led_strip[LED_STRIP_MATRIX]->refresh();
                i = (i + 1) % 5;
                vTaskDelayUntil(&xLastWakeTime, MATRIX_REFRESH_PERIOD / portTICK_PERIOD_MS);

            } while (xSemaphoreTake(LED_Strip_FSM_Semaphore, 0) == pdFALSE);
            // 信号量释放后，重新加载state_task
            state_task = state;
            break;
        }
        case LED_STRIP_HIT:
        {
            config_info = config->get_config_info_pt();
            // 命中图案，大符为对应环数（10环特殊，点亮），小符为1环
            switch (state_task.mode)
            {
            case PRA_RUNE_BIG_MODE:
                demux_led = LED_STRIP_MAIN_ARMOUR;
                led_strip[LED_STRIP_MAIN_ARMOUR]->clear_pixels();
                if (state_task.score == 10)
                {
                    // 满分：点亮 0,2,4,6,8 环（环0=最外圈，环8=最内圈）
                    for (uint16_t i = 0; i < 5; i++)
                        for (uint16_t j = hit_ring_cutoff[i * 2]; j < hit_ring_cutoff[i * 2 + 1]; j++)
                            led_strip[LED_STRIP_MAIN_ARMOUR]->set_color_index(j, state_task.color == PR_RED ? config_info->brightness : 0, 0, state_task.color == PR_RED ? 0 : config_info->brightness);
                }
                else
                {
                    // 普通得分：点亮 score 对应环（环0=最外圈）
                    for (uint16_t j = hit_ring_cutoff[state_task.score - 1]; j < hit_ring_cutoff[state_task.score]; j++)
                        led_strip[LED_STRIP_MAIN_ARMOUR]->set_color_index(j, state_task.color == PR_RED ? config_info->brightness : 0, 0, state_task.color == PR_RED ? 0 : config_info->brightness);
                }
                led_strip[LED_STRIP_MAIN_ARMOUR]->refresh();
                demux_led = LED_STRIP_ARM;
                led_strip[LED_STRIP_ARM]->set_color(state_task.color == PR_RED ? config_info->brightness_proportion_edge : 0, 0, state_task.color == PR_RED ? 0 : config_info->brightness_proportion_edge);
                led_strip[LED_STRIP_ARM]->refresh();
                demux_led = LED_STRIP_MATRIX;
                led_strip[LED_STRIP_MATRIX]->set_color(state_task.color == PR_RED ? config_info->brightness_proportion_matrix : 0, 0, state_task.color == PR_RED ? 0 : config_info->brightness_proportion_matrix);
                led_strip[LED_STRIP_MATRIX]->refresh();
                // 等待信号量
                xSemaphoreTake(LED_Strip_FSM_Semaphore, portMAX_DELAY);
                // 转移状态
                state_task = state;
                break;
            case PRA_RUNE_SMALL_MODE:
                demux_led = LED_STRIP_MAIN_ARMOUR;
                led_strip[LED_STRIP_MAIN_ARMOUR]->clear_pixels();
                // 小符命中锁定后亮最外圈(最外框)：环0 = hit_ring_cutoff[0]~[1]（物理最外圈48颗）
                for (uint16_t j = hit_ring_cutoff[0]; j < hit_ring_cutoff[1]; j++)
                {
                    led_strip[LED_STRIP_MAIN_ARMOUR]->set_color_index(j, state_task.color == PR_RED ? config_info->brightness : 0, 0, state_task.color == PR_RED ? 0 : config_info->brightness);
                }
                led_strip[LED_STRIP_MAIN_ARMOUR]->refresh();
                demux_led = LED_STRIP_ARM;
                led_strip[LED_STRIP_ARM]->set_color(state_task.color == PR_RED ? config_info->brightness_proportion_edge : 0, 0, state_task.color == PR_RED ? 0 : config_info->brightness_proportion_edge);
                led_strip[LED_STRIP_ARM]->refresh();
                demux_led = LED_STRIP_MATRIX;
                led_strip[LED_STRIP_MATRIX]->set_color(state_task.color == PR_RED ? config_info->brightness_proportion_matrix : 0, 0, state_task.color == PR_RED ? 0 : config_info->brightness_proportion_matrix);
                led_strip[LED_STRIP_MATRIX]->refresh();
                // 等待信号量
                xSemaphoreTake(LED_Strip_FSM_Semaphore, portMAX_DELAY);
                // 转移状态
                state_task = state;
                break;
            }
            break;
        }
        case LED_STRIP_BLINK:
        {

            config_info = config->get_config_info_pt();
            // 5块靶面同时闪烁，去掉按ID错开的延迟偏移
            // UPPER，LOWER，MATRIX，ARM闪烁十次，MAIN_ARMOUR不闪烁
            for (uint8_t i = 0; i < 10; i++)
            {
                demux_led = LED_STRIP_UPPER;
                led_strip[LED_STRIP_UPPER]->clear_pixels();
                led_strip[LED_STRIP_UPPER]->refresh();
                demux_led = LED_STRIP_LOWER;
                led_strip[LED_STRIP_LOWER]->clear_pixels();
                led_strip[LED_STRIP_LOWER]->refresh();
                demux_led = LED_STRIP_MATRIX;
                led_strip[LED_STRIP_MATRIX]->clear_pixels();
                led_strip[LED_STRIP_MATRIX]->refresh();
                demux_led = LED_STRIP_ARM;
                led_strip[LED_STRIP_ARM]->clear_pixels();
                led_strip[LED_STRIP_ARM]->refresh();
                vTaskDelay(100 / portTICK_PERIOD_MS);
                demux_led = LED_STRIP_UPPER;
                led_strip[LED_STRIP_UPPER]->set_color(state_task.color == PR_RED ? config_info->brightness : 0, 0, state_task.color == PR_RED ? 0 : config_info->brightness);
                led_strip[LED_STRIP_UPPER]->refresh();
                demux_led = LED_STRIP_LOWER;
                led_strip[LED_STRIP_LOWER]->set_color(state_task.color == PR_RED ? config_info->brightness : 0, 0, state_task.color == PR_RED ? 0 : config_info->brightness);
                led_strip[LED_STRIP_LOWER]->refresh();
                demux_led = LED_STRIP_MATRIX;
                led_strip[LED_STRIP_MATRIX]->set_color(state_task.color == PR_RED ? config_info->brightness_proportion_matrix : 0, 0, state_task.color == PR_RED ? 0 : config_info->brightness_proportion_matrix);
                led_strip[LED_STRIP_MATRIX]->refresh();
                demux_led = LED_STRIP_ARM;
                led_strip[LED_STRIP_ARM]->set_color(state_task.color == PR_RED ? config_info->brightness_proportion_edge : 0, 0, state_task.color == PR_RED ? 0 : config_info->brightness_proportion_edge);
                led_strip[LED_STRIP_ARM]->refresh();
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }
            // 等待信号量
            xSemaphoreTake(LED_Strip_FSM_Semaphore, portMAX_DELAY);
            // 转移状态
            state_task = state;
            break;
        }
        }
    }
    vTaskDelete(NULL);
}

void IRAM_ATTR PowerRune_Armour::GPIO_ISR_handler(void *arg)
{
    // 操作过程中激活互斥锁，屏蔽其他中断
    if (xSemaphoreTake(ISR_mutex, 0) == pdFALSE)
        return;
    uint8_t io = (*(uint8_t *)arg);
    // 发送事件
    PRA_HIT_EVENT_DATA hit_event_data;
    hit_event_data.address = config->get_config_info_pt()->armour_id - 1;
    hit_event_data.score = io;
    esp_event_post_to(pr_events_loop_handle, PRA, PRA_HIT_EVENT, &hit_event_data, sizeof(PRA_HIT_EVENT_DATA), portMAX_DELAY);
    xTaskCreate(restart_ISR_task, "restart_ISR_task", 4096, NULL, 5, NULL);
}

void PowerRune_Armour::GPIO_init()
{
    // 初始化GPIO
    gpio_config_t io_conf;
    // io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    for (uint8_t i = 0; i < 10; i++)
    {
        io_conf.pin_bit_mask = (1ULL << TRIGGER_IO[i]);
        gpio_config(&io_conf);
    }
}

void PowerRune_Armour::GPIO_polling_service(void *pvParameter)
{
    uint8_t io_valid_state[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    uint8_t io_last_valid_state[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    uint8_t io_last_reading[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    TickType_t last_jump_time[10] = {0};
    TickType_t bounce_time[10] = {0};
    TickType_t activation_time[10] = {0};
    TickType_t last_activation_time = 0;
    uint32_t bounce_count[10] = {0}; // 消抖计数

    ESP_LOGI(TAG_ARMOUR, "GPIO polling service start");
    TickType_t start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    while (1)
    {
        TickType_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        for (uint8_t i = 0; i < 10; i++)
        {
            int reading = gpio_get_level(TRIGGER_IO[i]);

            // 去抖动处理
            if (reading != io_last_reading[i] && valid[i])
            {
                last_jump_time[i] = current_time;
                bounce_count[i]++;
                ESP_LOGI(TAG_ARMOUR, "GPIO %d, Bounce Count: %d", (int)TRIGGER_IO[i], (int)bounce_count[i]);
                io_last_reading[i] = reading;
            }

            if ((current_time - last_jump_time[i]) > 1) // 消抖时间
            {
                if (valid[i] && (reading != io_valid_state[i])) // 有效触发
                {
                    if (reading == 0 && current_time - last_activation_time > 1000) // LOW level，触发间隔需大于1s
                    {
                        io_valid_state[i] = reading;
                        last_activation_time = current_time;
                        activation_time[i] = current_time;
                        ESP_LOGI(TAG_ARMOUR, "GPIO %d, Score IO %d Triggered", (int)TRIGGER_IO[i], (int)TRIGGER_IO_TO_SCORE[i]);
                    }
                    else if (reading == 1)
                    {
                        io_valid_state[i] = reading;
                        ESP_LOGI(TAG_ARMOUR, "GPIO %d, Score IO %d Released", (int)TRIGGER_IO[i], (int)TRIGGER_IO_TO_SCORE[i]);
                    }
                }
            }

            // 持续低电平激活检测
            if (io_valid_state[i] == 0 && current_time - activation_time[i] > 1000 && valid[i])
            {
                valid[i] = false;
                ESP_LOGI(TAG_ARMOUR, "GPIO %d damaged: Low level detected for too long.", TRIGGER_IO[i]);
                io_valid_state[i] = 1;
            }

            // 跳变检测，防止键轴卡阻
            if ((current_time - bounce_time[i]) < 5000 && valid[i])
            {
                if (bounce_count[i] > 10) // 5秒内跳变次数超过15次，认为是键轴卡阻
                {
                    valid[i] = false;
                    ESP_LOGI(TAG_ARMOUR, "GPIO %d damaged: Frequent bouncing detected.", TRIGGER_IO[i]);
                    io_valid_state[i] = 1;
                    bounce_count[i] = 0;
                    bounce_time[i] = current_time;
                }
            }
            else if (valid[i])
            {
                bounce_count[i] = 0;
                bounce_time[i] = current_time;
            }
            else if (!valid[i])
            {
                bounce_count[i] = 0;
            }

            if ((xTaskGetTickCount() * portTICK_PERIOD_MS - start_time) > 2000) // 预留2s供按键检测
            {
                if (io_valid_state[i] == 0 && valid[i] && io_last_valid_state[i] == 1) // 下升沿触发，保证实时性
                {
                    ESP_LOGI(TAG_ARMOUR, "GPIO %d, Score IO %d Sending event...", TRIGGER_IO[i], TRIGGER_IO_TO_SCORE[i]);
                    // 发送事件
                    PRA_HIT_EVENT_DATA hit_event_data;
                    hit_event_data.address = config->get_config_info_pt()->armour_id - 1;
                    hit_event_data.score = TRIGGER_IO_TO_SCORE[i];
                    esp_event_post_to(pr_events_loop_handle, PRA, PRA_HIT_EVENT, &hit_event_data, sizeof(PRA_HIT_EVENT_DATA), portMAX_DELAY);
                    io_last_valid_state[i] = io_valid_state[i];
                }
                else if (io_valid_state[i] == 1 && valid[i] && io_last_valid_state[i] == 0)
                {
                    io_last_valid_state[i] = io_valid_state[i];
                }
            }
        }
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}

void PowerRune_Armour::GPIO_ISR_enable()
{
    // 初始化GPIO ISR
    for (uint8_t i = 0; i < 10; i++)
    {
        gpio_set_intr_type(TRIGGER_IO[i], GPIO_INTR_NEGEDGE);
    }
}

void PowerRune_Armour::restart_ISR_task(void *pvParameter)
{
    // 屏蔽1s
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    // 释放信号量
    xSemaphoreGive(ISR_mutex);
    vTaskDelete(NULL);
}

// Class PowerRune_Armour 定义
PowerRune_Armour::PowerRune_Armour()
{
    // 初始化GPIO
    GPIO_init();
    // 开启ISR服务
    // gpio_install_isr_service(0);
    // for (uint8_t i = 0; i < 10; i++)
    // {
    // gpio_isr_handler_add(TRIGGER_IO[i], GPIO_ISR_handler, (void *)&TRIGGER_IO_TO_SCORE[i]);
    // }

    // GPIO_ISR_enable();
    // 开启GPIO轮询服务
    xTaskCreate(GPIO_polling_service, "GPIO_polling_service", 4096, NULL, 5, NULL);
    // 初始化LED_Strip
    led_strip[LED_STRIP_MAIN_ARMOUR] = new LED_Strip(STRIP_IO, 271); // 实际PCB主环DIN0=271颗
    led_strip[LED_STRIP_UPPER] = new LED_Strip(STRIP_IO, 86);
    led_strip[LED_STRIP_LOWER] = new LED_Strip(STRIP_IO, 92);
    led_strip[LED_STRIP_ARM] = new LED_Strip(STRIP_IO, 53); // 实际PCB灯臂53颗(12+10+9+10+12)
    led_strip[LED_STRIP_MATRIX] = new LED_Strip(STRIP_IO, 165);

    // 状态机更新信号量
    LED_Strip_FSM_Semaphore = xSemaphoreCreateBinary();
    xSemaphoreGive(ISR_mutex);
    // 创建LED更新任务
    xTaskCreate(LED_update_task, "LED_update_task", 8192, NULL, 5, &LED_update_task_handle);

    // 注册装甲板事件处理
    ESP_ERROR_CHECK(esp_event_handler_register_with(pr_events_loop_handle, PRA, PRA_START_EVENT, global_pr_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register_with(pr_events_loop_handle, PRA, PRA_HIT_EVENT, global_pr_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register_with(pr_events_loop_handle, PRA, PRA_STOP_EVENT, global_pr_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register_with(pr_events_loop_handle, PRA, PRA_COMPLETE_EVENT, global_pr_event_handler, NULL));
    // 2026: 大符激活进度(漏注册这行的症状是"进度灯效完全不显示")
    ESP_ERROR_CHECK(esp_event_handler_register_with(pr_events_loop_handle, PRA, PRA_PROGRESS_EVENT, global_pr_event_handler, NULL));
    // OTA事件处理
    ESP_ERROR_CHECK(esp_event_handler_register_with(pr_events_loop_handle, PRC, OTA_BEGIN_EVENT, global_pr_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register_with(pr_events_loop_handle, PRC, OTA_COMPLETE_EVENT, global_pr_event_handler, NULL));
}

void PowerRune_Armour::trigger(RUNE_MODE mode, RUNE_COLOR color)
{
    ESP_LOGI(TAG_ARMOUR, "Trigger Armour with mode: %s, color: %s", mode == PRA_RUNE_BIG_MODE ? "Big" : "Small", color == PR_RED ? "Red" : "Blue");
    // 状态机更新
    state.LED_Strip_State = LED_STRIP_TARGET;
    state.mode = mode;
    state.color = color;
    state.score = 0;
    // 释放信号量
    xSemaphoreGive(LED_Strip_FSM_Semaphore);
}

void PowerRune_Armour::stop()
{
    ESP_LOGI(TAG_ARMOUR, "Stop Armour");
    // 状态机更新
    state.LED_Strip_State = LED_STRIP_IDLE;
    // 释放信号量
    xSemaphoreGive(LED_Strip_FSM_Semaphore);
}

void PowerRune_Armour::debug()
{
    ESP_LOGI(TAG_ARMOUR, "Debug Armour");
    // 状态机更新
    state.LED_Strip_State = LED_STRIP_DEBUG;
    // 释放信号量
    xSemaphoreGive(LED_Strip_FSM_Semaphore);
}

void PowerRune_Armour::hit(uint8_t score)
{
    ESP_LOGI(TAG_ARMOUR, "Hit Armour with score: %d", score);
    // 状态机更新
    state.LED_Strip_State = LED_STRIP_HIT;
    state.score = score;
    // 释放信号量
    xSemaphoreGive(LED_Strip_FSM_Semaphore);
}

void PowerRune_Armour::blink()
{
    ESP_LOGI(TAG_ARMOUR, "Activation Complete, Blink Armour");
    // 状态机更新
    state.LED_Strip_State = LED_STRIP_BLINK;
    // 释放信号量
    xSemaphoreGive(LED_Strip_FSM_Semaphore);
}

void PowerRune_Armour::global_pr_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    if (base == PRA)
        switch (id)
        {
        case PRA_START_EVENT:
        {
            PRA_START_EVENT_DATA *start_event_data = (PRA_START_EVENT_DATA *)event_data;
            trigger((RUNE_MODE)start_event_data->mode, (RUNE_COLOR)start_event_data->color);
            break;
        }
        case PRA_STOP_EVENT:
        {
            // 颜色修复: STOP 现在可以携带颜色（PR_COLOR_KEEP=保持当前，见 PowerRune_Events.h）。
            // 主控只在点亮某块时才随 START 下发颜色，没被点亮的符面会一直保留上一轮的颜色 ——
            // 切红蓝后再开一轮，那些符面在收尾闪烁/待机进度条上就是错的颜色。
            // 主控开符时会向全部 5 块广播一次带颜色的 STOP，靠这里把颜色落到 state 上。
            PRA_STOP_EVENT_DATA *stop_event_data = (PRA_STOP_EVENT_DATA *)event_data;
            if (stop_event_data != NULL && stop_event_data->color != PR_COLOR_KEEP)
            {
                ESP_LOGI(TAG_ARMOUR, "Stop with color sync: %s", stop_event_data->color == PR_RED ? "Red" : "Blue");
                state.color = (RUNE_COLOR)stop_event_data->color;
            }
            stop();
            break;
        }
        case PRA_HIT_EVENT:
        {
            // 检查状态机状态
            if (state.LED_Strip_State == LED_STRIP_TARGET)
            {
                PRA_HIT_EVENT_DATA *hit_event_data = (PRA_HIT_EVENT_DATA *)event_data;
                hit(hit_event_data->score);
            }
            break;
        }
        case PRA_COMPLETE_EVENT:
        {
            blink();
            break;
        }
        case PRA_PROGRESS_EVENT:
        {
            // 2026: 只写共享状态, 绝不能在这里碰 LED / demux。
            // 本 handler 跑在 pr_events_loop 任务, 与 LED_update_task 共享全局 demux_led(3 位地址线)
            // 和唯一一个 RMT 通道; 在这里画灯会串通道, 且 refresh() 默认阻塞会把事件循环
            // 连同 ESP-NOW 收发一起顶死。渲染统一放到 LED_update_task 里做。
            PRA_PROGRESS_EVENT_DATA *progress_event_data = (PRA_PROGRESS_EVENT_DATA *)event_data;
            uint8_t total = progress_event_data->total_groups ? progress_event_data->total_groups : 5;
            uint8_t done = progress_event_data->activated_groups;
            if (done > total)
                done = total; // 钳位, 防对端发脏数据导致越界点亮
            activation_total = total;
            activation_progress = done;
            ESP_LOGI(TAG_ARMOUR, "Activation progress: %d/%d", done, total);
            // 唤醒LED_update_task刷新IDLE状态的灯臂进度条(不碰LED/demux, 只发信号量)
            if (state.LED_Strip_State == LED_STRIP_IDLE)
                xSemaphoreGive(LED_Strip_FSM_Semaphore);
            break;
        }
        }
    else if (base == PRC)
    {
        switch (id)
        {
        case OTA_BEGIN_EVENT:
            debug();
            break;
        case OTA_COMPLETE_EVENT:
            stop();
            break;
        }
    }
}
