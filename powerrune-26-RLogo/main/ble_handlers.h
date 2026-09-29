/**
 * @file "ble_handlers.h"
 * @note 本文件存放蓝牙事件回调函数
 * @version 1.0
 * @date 2024-02-19
 */
#pragma once
#ifndef _BLE_HANDLERS_H_
#define _BLE_HANDLERS_H_
#include "main.h"

const char *TAG_BLE = "BLE";

// ===== 蓝牙修复: 统一的通知发送入口 =====
// 原来 37 处直接调用 esp_ble_gatts_send_indicate，有三个问题：
//   1) 不判断是否还有连接 —— 断连后仍用陈旧的 conn_id/gatts_if 反复发；
//   2) 句柄表还没注册完成(句柄为 0)时也会发；
//   3) 返回值从不检查，发失败/拥塞时无人知道。
// 现在统一走这里：连接与句柄校验 → 发送 → 检查返回值并计数。
static uint32_t ble_notify_fail_count = 0; // 发送失败/被拒次数
static uint32_t ble_notify_skip_count = 0; // 未连接/句柄未就绪而跳过次数
static bool s_ble_congested = false;       // 由 ESP_GATTS_CONGEST_EVT 维护

static inline bool ble_notify(uint16_t handle, const char *msg)
{
    if (!is_connected || spp_conn_id == 0xffff || spp_gatts_if == 0xff)
    {
        // 上位机没连着就没必要发(发了也看不到)，也不再刷协议栈的错误日志
        ble_notify_skip_count++;
        return false;
    }
    if (handle == 0)
    {
        // 句柄表还没注册完成
        ble_notify_skip_count++;
        ESP_LOGW(TAG_BLE, "notify skipped: handle not ready");
        return false;
    }
    // 通知载荷上限是 MTU-3。正常上位机(本地 MTU 设 500)不会超；
    // 超了只告警不截断 —— 截断会破坏上位机对 "...Failed" 结尾的判断
    if (spp_mtu_size > 3 && strlen(msg) + 1 > (size_t)(spp_mtu_size - 3))
        ESP_LOGW(TAG_BLE, "notify payload %u > MTU-3 (%u), may fail",
                 (unsigned)(strlen(msg) + 1), (unsigned)(spp_mtu_size - 3));
    esp_err_t err = esp_ble_gatts_send_indicate(spp_gatts_if, spp_conn_id, handle,
                                                strlen(msg) + 1, (uint8_t *)msg, false);
    if (err != ESP_OK)
    {
        ble_notify_fail_count++;
        ESP_LOGW(TAG_BLE, "notify failed (%s), fail=%lu skip=%lu%s", esp_err_to_name(err),
                 (unsigned long)ble_notify_fail_count, (unsigned long)ble_notify_skip_count,
                 s_ble_congested ? ", congested" : "");
        return false;
    }
    return true;
}

// 大符操作服务的通知(启动/停止/OTA/得分回显)
static inline bool notify_ops(uint8_t ops_val_idx, const char *msg)
{
    return ble_notify(ops_handle_table[ops_val_idx], msg);
}

// 系统参数设置服务的通知(URL/SSID/复位装甲ID)
static inline bool notify_spp(uint8_t spp_val_idx, const char *msg)
{
    return ble_notify(spp_handle_table[spp_val_idx], msg);
}

// ===== 蓝牙修复: 断连时清理长写(prepare write)缓存 =====
// store_wr_buffer() 分配后从不释放，断连也不清理 —— 既漏内存，
// 又可能让上一次连接的长写残留被带到下一次连接。
static inline void clear_wr_buffer(void)
{
    spp_receive_data_node_t *node = SppRecvDataBuff.first_node;
    while (node != NULL)
    {
        spp_receive_data_node_t *next = node->next_node;
        free(node->node_buff);
        free(node);
        node = next;
    }
    SppRecvDataBuff.first_node = NULL;
    SppRecvDataBuff.node_num = 0;
    SppRecvDataBuff.buff_size = 0;
    temp_spp_recv_data_node_p1 = NULL;
    temp_spp_recv_data_node_p2 = NULL;
}

// ===== 蓝牙修复: 断连时把连接相关状态一次性清干净 =====
// 原代码只清 is_connected/enable_data_ntf，spp_conn_id 永远停在上一个连接上，
// 之后所有通知都是拿着过期的 conn_id 发的。
static inline void ble_on_disconnect(void)
{
    is_connected = false;
    enable_data_ntf = false;
    spp_conn_id = 0xffff;
    s_ble_congested = false;
    clear_wr_buffer();
}

// 函数作用；找到handle对应的index
static uint8_t find_char_and_desr_index(uint16_t handle)
{
    uint8_t error = 0xff;
    // 系统参数设置服务
    if (handle < spp_handle_table[0] + SPP_IDX_NB)
    {
        for (int i = 0; i < SPP_IDX_NB; i++)
        {
            if (handle == spp_handle_table[i])
            {
                return i;
            }
        }
    }
    // 大符操作服务
    else
    {
        for (int i = 0; i < OPS_IDX_NB; i++)
        {
            if (handle == ops_handle_table[i])
            {
                return i + SPP_IDX_NB;
            }
        }
    }
    return error;
}

static bool store_wr_buffer(esp_ble_gatts_cb_param_t *p_data)
{
    temp_spp_recv_data_node_p1 = (spp_receive_data_node_t *)malloc(sizeof(spp_receive_data_node_t));

    if (temp_spp_recv_data_node_p1 == NULL)
    {
        ESP_LOGI(GATTS_TABLE_TAG, "malloc error %s %d\n", __func__, __LINE__);
        return false;
    }
    if (temp_spp_recv_data_node_p2 != NULL)
    {
        temp_spp_recv_data_node_p2->next_node = temp_spp_recv_data_node_p1;
    }
    temp_spp_recv_data_node_p1->len = p_data->write.len;
    SppRecvDataBuff.buff_size += p_data->write.len;
    temp_spp_recv_data_node_p1->next_node = NULL;
    temp_spp_recv_data_node_p1->node_buff = (uint8_t *)malloc(p_data->write.len);
    temp_spp_recv_data_node_p2 = temp_spp_recv_data_node_p1;
    memcpy(temp_spp_recv_data_node_p1->node_buff, p_data->write.value, p_data->write.len);
    if (SppRecvDataBuff.node_num == 0)
    {
        SppRecvDataBuff.first_node = temp_spp_recv_data_node_p1;
        SppRecvDataBuff.node_num++;
    }
    else
    {
        SppRecvDataBuff.node_num++;
    }

    return true;
}

// GATTS最终的回调函数
void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

// GATTS的回调函数
void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    ESP_LOGD(TAG_BLE, "\nGATTS事件回调\n\n");
    ESP_LOGI(GATTS_TABLE_TAG, "EVT %d, gatts if %d\n", event, gatts_if);

    /* If event is register event, store the gatts_if for each profile */
    // 如果是注册APP事件
    if (event == ESP_GATTS_REG_EVT)
    {
        // 如果注册成功
        if (param->reg.status == ESP_GATT_OK)
        {
            // APP记录描述符
            spp_profile_tab[SPP_PROFILE_APP_IDX /*只用一个APP所以下标为0*/].gatts_if = gatts_if;
        }
        else
        {
            ESP_LOGI(GATTS_TABLE_TAG, "Reg app failed, app_id %04x, status %d\n", param->reg.app_id, param->reg.status);
            return;
        }
    }
    do
    {
        int idx;
        // 遍历注册APP的结构表
        for (idx = 0; idx < SPP_PROFILE_NUM; idx++)
        {
            if (gatts_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                gatts_if == spp_profile_tab[idx].gatts_if)
            {
                // 执行注册的APP的回调函数
                if (spp_profile_tab[idx].gatts_cb)
                {
                    ESP_LOGD(TAG_BLE, "去往profile\n");
                    spp_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
    ESP_LOGD(TAG_BLE, "从profile回来\n");
}

// GAP的回调函数
void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    ESP_LOGD(TAG_BLE, "GAP的回调函数\n");
    esp_err_t err;
    ESP_LOGE(GATTS_TABLE_TAG, "GAP_EVT, event %d\n", event);

    switch (event)
    {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        // 设置广播原始数据完成
        esp_ble_gap_start_advertising(&spp_adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        // 开始广播完成
        //  advertising start complete event to indicate advertising start successfully or failed
        if ((err = param->adv_start_cmpl.status) != ESP_BT_STATUS_SUCCESS)
            ESP_LOGE(GATTS_TABLE_TAG, "Advertising start failed: %s\n", esp_err_to_name(err));

        break;
    default:
        break;
    }
}
#endif