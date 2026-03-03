#include "mgmt_netlink.h"
#include "mgmt_types.h"
#include <stdio.h>
#include <string.h>
#include "mgmt_transmit.h"
#include "sqlite_unit.h"
#include <arpa/inet.h>
/*
 * mgmt_netlink_set_param
 * - 功能: 接收上层（UI 或 web）要下发的参数缓冲，原版会通过 netlink 发送到内核/驱动
 * - 参数: buf - 指向要下发的原始缓冲（通常包含 Smgmt_header + Smgmt_set_param）
 *           len - 缓冲区长度
 *           ifname - 可选网口名（未使用）
 * - 返回: 0 成功，非 0 表示失败
 * - 说明: 在当前工程中此函数为 Mock，实现为拦截并打印，真正下发由内核 netlink 实现
 */

int mgmt_netlink_set_param(char *buf, int len, char *ifname) {
    Smgmt_header* mhead = (Smgmt_header*)buf;
    Smgmt_set_param* mparam = (Smgmt_set_param*)mhead->mgmt_data;

    uint16_t type = ntohs(mhead->mgmt_type);
    int need_save = 0;

    printf("\n[下发中心] 收到网页配置下发指令！\n");

    if (type & MGMT_SET_FREQUENCY) {
        uint32_t real_freq = ntohl(mparam->mgmt_mac_freq);
        if (real_freq > 3000 || real_freq < 100) real_freq = mparam->mgmt_mac_freq; 
        FREQ_INIT = real_freq;
        need_save = 1;
    }
    if (type & MGMT_SET_BANDWIDTH) {
        int bw_index = mparam->mgmt_mac_bw;
        if (bw_index == 0) BW_INIT = 20;
        else if (bw_index == 1) BW_INIT = 10;
        else if (bw_index == 2) BW_INIT = 5;
        else if (bw_index == 3) BW_INIT = 3; 
        need_save = 1;
    }
    if (type & MGMT_SET_POWER) {
        int power_encoded = ntohs(mparam->mgmt_mac_txpower); 
        if (power_encoded > 100) power_encoded = mparam->mgmt_mac_txpower;
        if (DEVICETYPE_INIT == 3) POWER_INIT = power_encoded; 
        else POWER_INIT = 39 - power_encoded; 
        need_save = 1;
    }
    if (type & MGMT_SET_UNICAST_MCS) {
        MCS_INIT = mparam->mgmt_virt_unicast_mcs;
        need_save = 1;
    }

    if (need_save) {
        // 1. 保存到系统文件
        save_config_to_file();
        
        // 2. 【关键】通知 5秒心跳线程 去安全地刷新数据库下拉框！
        g_config_dirty = 1; 
        printf("[下发中心] 内存更新完毕，等待心跳线程同步至网页...\n\n");
    }
    return 0;
}

/*
 * mgmt_netlink_get_info
 * - 功能: 按 type/cmd 查询并填充 out_buf，原版通过 netlink 从内核/驱动读取状态
 * - 参数: type  - 查询类型（含义由上层协议定义）
 *           cmd   - 具体命令（例如 MGMT_CMD_GET_VETH_INFO）
 *           ifname- 可选接口名（未使用）
 *           out_buf - 输出缓冲，用于写入返回的结构（例如 struct mgmt_send）
 * - 返回: 0 成功，非 0 失败
 * - 说明: 当前实现为 Mock，会伪造 0x07/0x09 等协议帧所需的数据，供 UI 层读取显示。
 */
// 拦截应用层向底层的状态查询请求
int mgmt_netlink_get_info(int type, int cmd, char *ifname, char *out_buf) {
    if (cmd == MGMT_CMD_GET_VETH_INFO) {
        struct mgmt_send *self_msg = (struct mgmt_send *)out_buf;
        memset(self_msg, 0, sizeof(struct mgmt_send));

        /* 1. 伪造 0x07 指令所需的硬件体检数据 */
        self_msg->amp_infomation.battery_level = 85;      // 电池电量 85%
        self_msg->amp_infomation.temperature = 45;        // 主板温度 45℃
        self_msg->amp_infomation.fan_status = 1;          // 风机转速状态正常
        self_msg->amp_infomation.rf_tx_power_status = 1;  // 射频状态正常
        self_msg->amp_infomation.rf_ch1_temp1 = 30;       // 通道1温度 30℃

        /* 2. 伪造 0x09 指令所需的组网邻居节点数据 */
        self_msg->neigh_num = 1;                          // 假装网络中发现了 1 个邻居
        self_msg->msg[0].node_id = 2;                     // 邻居节点的 ID 是 2
        self_msg->msg[0].rssi = 60;                       // 邻居节点的信号强度 (负值, 60表示-60dBm)
        self_msg->msg[0].time_jitter = 15;                // 传输时延 15ms
    }
    return 0;
}

