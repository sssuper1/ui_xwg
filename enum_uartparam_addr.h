#ifndef PARAM_ADDR_H
#define PARAM_ADDR_H

/*
 * enum_uartparam_addr.h
 * 说明：本文件定义了 UI/工控屏与系统之间通过 UART 或管理协议传递的参数地址常量。
 * 每个宏代表一个参数的地址编码（32bit），上位机或触摸屏通过该地址指定要读/写的字段。
 * 使用时请确保与前端/协议解析保持一致。文件中的注释按功能分组逐项列出。
 */

// 工控屏参数地址汇总

#define PARAM_0A_REQUEST_ADDR                    0x10000000  //0a命令 节点详细信息

/****************************** 操作-模式设置 ******************************/
#define PARAM_OP_MODE_CURRENT_WORK_MODE       0x11100000  // 当前工作模式
#define PARAM_OP_MODE_CANCELLATION_STATE      0x11200000  // 对消状态
#define PARAM_OP_MODE_SPATIAL_FILTERING       0x11300000  // 空域滤波
#define PARAM_OP_MODE_COLOCATION_MODE         0x11400000  // 共址模式
#define PARAM_OP_MODE_ADAPTIVE_FREQ_SELECT    0x11500000  // 自适应选频
#define PARAM_OP_MODE_SYNC_MODE               0x11600000  // 内外同步模式
#define PARAM_OP_MODE_SERVICE_MODE            0x11700000  // 业务模式

/****************************** 操作-参数设置 ******************************/
#define PARAM_OP_PARAM_REMOTE_MODIFY          0x12100000  // 远程参数修改
#define PARAM_OP_PARAM_UPDATE_AND_NET_START   0x12200000  // 更新参数并启动入网

/****************************** 通道1设置 ******************************/
#define PARAM_CH1_ROUTING_PROTOCOL            0x12310000  // 路由协议
#define PARAM_CH1_ACCESS_PROTOCOL             0x12320000  // 接入协议
#define PARAM_CH1_TIME_REFERENCE              0x12330000  // 时间基准
#define PARAM_CH1_FREQ_HOPPING_MODE           0x12340000  // 跳频方式

/****************************** 通道1-工作频率设置 ******************************/
#define PARAM_CH1_FIXED_FREQ_CENTER           0x12351000  // 点频-中心频率
//#define PARAM_CH1_HOPPING_PATTERN_NUM         0x12352000  // 跳频-跳频图样号
#define PARAM_CH1_SELECTED_FREQ_1             0x12352000  //通道1设置-工作频率-自适应-中心频率1
#define PARAM_CH1_SELECTED_FREQ_2             0x12353000  //通道1设置-工作频率-自适应-中心频率2
#define PARAM_CH1_SELECTED_FREQ_3             0x12354000  //通道1设置-工作频率-自适应-中心频率3
#define PARAM_CH1_SELECTED_FREQ_4             0x12355000  //通道1设置-工作频率-自适应-中心频率4


/****************************** 通道1-跳频频率集设置 ******************************/
//#define PARAM_CH1_HOPPING_FREQ_COUNT          0x12361000  // 跳频频点个数

/****************************** 通道1-信号参数 ******************************/
#define PARAM_CH1_SIGNAL_BANDWIDTH            0x12370000  // 信号带宽

/****************************** 通道1-调制方式 ******************************/
//#define PARAM_CH1_MODULATION_NARROWBAND       0x12381000  // 窄带
#define PARAM_CH1_MODULATION_WIDEBAND         0x12382000  // 宽带

/****************************** 通道1-编码参数 ******************************/
#define PARAM_CH1_CODING_EFFICIENCY           0x12390000  // 编码效率

/****************************** 通道1-发射功率 ******************************/
#define PARAM_CH1_TX_POWER_NARROWBAND         0x123A1000  // 窄带波形
#define PARAM_CH1_TX_POWER_MIMO               0x123A2000  // MIMO
#define PARAM_CH1_TX_POWER_SPREAD_HOP         0x123A3000  // 扩跳
#define PARAM_CH1_TX_POWER_ATTENUATION        0x123B0000  // 发射功率衰减


// 功能-收发消息统计-操作
#define PARAM_TXRX_INFO_OPERATION             0x23900000  //00-开始统计 01-停止统计 02-计数清零

#endif