#include "mgmt_types.h" 
#include "ui_get.h"
#include "enum_uartparam_addr.h"
#include <stdbool.h>
#include <stdlib.h>    /* malloc, free, system */
#include <string.h>    /* memcpy, memset, strlen */
#include <stdio.h>     /* sprintf */
#include "sqlite_unit.h"
#include "mgmt_netlink.h"
#include "gpsget.h"
#include "socketUDP.h"
#include <errno.h>

//global struct
extern int ui_fd;
#ifndef HAVE_GET_INTERFACE_STATS_PROTO
/* prototype for helper defined later in this file */
int get_interface_stats(Info_0x06_Statistics* info_stat);
#endif
#define IMX6ULL_MOCK_TEST  1  // 1:开启脱离硬件的纯协议测试，0:恢复原版

extern Global_Radio_Param g_radio_param;
//extern uint8_t SELFID;
extern GPS_INFO gps_info_uart;
//extern uint32_t FREQ_INIT;
Info_0x06_Statistics stat_info;
uint8_t SELFID;              
uint32_t FREQ_INIT ;       

/*
 * int_to_little_endian
 * - 功能: 将 32 位整数进行字节序翻转（主机序 <-> 小端序），用于协议或帧字段的字节序处理。
 * - 参数: value - 要转换的 32 位无符号整数。
 * - 返回: 翻转后的 32 位无符号整数。
 * - 说明: 在本项目中，某些协议字段需要固定的字节序，本函数用于在组包/解析前进行转换。
 */
uint32_t int_to_little_endian(uint32_t value) {
    return ((value & 0x000000FF) << 24) |  // 最低字节移到最高位
           ((value & 0x0000FF00) << 8)  |  // 次低字节左移 8 位
           ((value & 0x00FF0000) >> 8)  |  // 次高字节右移 8 位
           ((value & 0xFF000000) >> 24);   // 最高字节移到最低位
}


/*
 * CRC_Check
 * - 功能: 计算数据缓冲区的 CRC 校验值 (基于 0xA001 多项式的循环冗余校验)
 * - 参数: CRC_Ptr - 指向要校验的数据起始地址
 *           LEN - 要校验的字节长度
 * - 返回: 16 位校验值（字节高低位最终进行了位置交换以适配发送格式）
 * - 说明: 用于生成 UART/串口协议帧尾部的校验字段，发送前通常将返回值按网络序写入帧。
 */
uint16_t CRC_Check(uint8_t* CRC_Ptr, uint16_t LEN)
{
    uint16_t CRC_Value = 0xffff;
    for(int i=0;i<LEN;i++)
    {
        CRC_Value ^= CRC_Ptr[i];
        for(int j=0;j<8;j++)
        {
            if(CRC_Value & 0x0001)
                CRC_Value = (CRC_Value>>1)^0xA001;
            else
                CRC_Value = (CRC_Value >> 1);
        }
    }

    CRC_Value = ((CRC_Value>>8)  |  (CRC_Value << 8));
    return CRC_Value;
}


/*
 * Send_0x04
 * - 功能: 根据本地配置参数构造并通过串口/文件描述符发送 0x04 配置帧。
 * - 参数: fd   - 打开的串口/设备文件描述符（写出到 UI 屏）
 *           info - 指向 Node_Xwg_Pairs 数组的指针（来自 /etc/node_xwg 解析），size 为字节长度
 *           size - info 指向的数据长度（字节）
 * - 帧中重点字段说明:
 *     current_work_mode - 节点当前工作模式（取自 macmode）
 *     spatial_filter    - 空域滤波（0 开启/1 关闭），对应 UI 的 kylb 设置
 *     Channel1.center_freq - 中心频率，单位为 kHz（代码中乘以 1000 得到 Hz 表示）
 *     signal_bw / mod_wide - 带宽/调制参数（对应 bw / mcs）
 * - 说明: 该函数只负责组帧与发送，参数由上层（读取 /etc/node_xwg）传入。
 */
int8_t Send_0x04(int fd,void* info,int size)
{
    if (fd < 0 || info == NULL || size <= 0) {
        return -1;
    }

    Frame_04_byte frame_send ;
    memset(&frame_send,0,sizeof(Frame_04_byte));
    int BYTE04SIZE=sizeof(Frame_04_byte);

    uint8_t send_byte_stream[BYTE04SIZE] ;
    uint8_t byte_stream_recv[9] ;

    Node_Xwg_Pairs *param_pairs=(Node_Xwg_Pairs *)malloc(size);
    if (!param_pairs) {
        return -1;
    }
    memcpy(param_pairs,(Node_Xwg_Pairs*)info,size);


    frame_send.head = htons(0xD55D);
    frame_send.dirrection = 0x04;
    frame_send.message_type = 0xFF;

    frame_send.current_work_mode = (uint8_t)get_int_value((void*)param_pairs,"macmode");      // 当前工作模式 0:宽带电台
    frame_send.spatial_filter = 0x01;           // 空域滤波 0：开启 1：关闭
    if(get_int_value((void*)param_pairs,"kylb")==KYLB_MODE_OPEN)
    {
        frame_send.spatial_filter = 0;      
    }

    if(get_int_value((void*)param_pairs,"workmode")==WORK_MODE_TYPE_DP)
    {
        frame_send.Channel1.hopping_mode = 0x00; // 跳频方式 定频
    }
    if(get_int_value((void*)param_pairs,"workmode")==WORK_MODE_TYPE_ZSYXP)
    {
        frame_send.Channel1.hopping_mode = 1;      // 跳频方式 自适应选频
    }

    frame_send.Channel1.route_protocol = (uint8_t)get_int_value((void*)param_pairs,"router")-1;  // 路由协议
    frame_send.Channel1.access_protocol = 0x0;// 接入协议

    frame_send.Channel1.center_freq    = get_int_value((void*)param_pairs,"channel")*1000;// 中心频率（点频） *1000
    frame_send.Channel1.select_freq_1  = get_int_value((void*)param_pairs,"select_freq1")*1000;// 自适应-中心频率1 *1000
    frame_send.Channel1.select_freq_2  = get_int_value((void*)param_pairs,"select_freq2")*1000;// 自适应-中心频率2 *1000
    frame_send.Channel1.select_freq_3  = get_int_value((void*)param_pairs,"select_freq3")*1000;// 自适应-中心频率3 *1000
    frame_send.Channel1.select_freq_4  = get_int_value((void*)param_pairs,"select_freq4")*1000;// 自适应-中心频率4 *1000

    frame_send.Channel1.signal_bw = (uint8_t)get_int_value((void*)param_pairs,"bw");        // 信号带宽

    frame_send.Channel1.mod_wide =(uint8_t)get_int_value((void*)param_pairs,"mcs");
    frame_send.sync_mode=(uint8_t)get_int_value((void*)param_pairs,"sync_mode");

    frame_send.Channel1.tx_power_spread = 0;// 发射功率
    frame_send.Channel1.tx_attenuation = 0;// 发射功率衰减

    frame_send.check = htons(CRC_Check(&frame_send.dirrection, BYTE04SIZE-6));                  //校验
    frame_send.tail = htons(0x5DD5);
    memcpy(send_byte_stream,&frame_send,BYTE04SIZE);

    write(fd,(void*)send_byte_stream,BYTE04SIZE);
    sleep(1);
    
    free(param_pairs); // 记得释放内存
    return 0;
}

/*
 * Send_0x05
 * - 功能: 发送 0x05 节点状态帧（设备信息 / GPS / 串口/端口等信息）
 * - 参数: fd   - 串口/设备文件描述符
 *           info - 指向 Global_Radio_Param 的指针（可含平台收集到的参数）
 * - 帧字段要点:
 *     device_id - 本机节点 ID（全局 SELFID）
 *     device_name - 设备名称，格式 kddt-<SELFID>
 *     service_ip/service_port - 业务网口 IP 与端口
 *     gps_auto_sync, longitude, latitude, altitude - GPS 位置信息与自动同步标志
 *     manual_hour/minute/second - 手动时间值（若未开启自动同步）
 * - 说明: 此帧用于向 UI 上报设备基本信息与当前 GPS/时间数据。
 */
int8_t Send_0x05(int fd,void* info)
{
    if (fd < 0 || info == NULL) {
        return -1;
    }

    double t_lon,t_lat;
    uint16_t t_alit;

    Frame_05_byte frame_send ;
    int BYTE05SIZE=sizeof(Frame_05_byte);
    memset(&frame_send,0,BYTE05SIZE);
    
    uint8_t send_byte_stream[BYTE05SIZE] ;

    Global_Radio_Param param;
    memcpy(&param,(Global_Radio_Param*)info,sizeof(Global_Radio_Param));

    frame_send.head = htons(0xD55D);
    frame_send.dirrection = 0x05;
    frame_send.message_type = 0xFF;
    
     // 设备ID
    frame_send.value.device_id = SELFID;             

    /* 设备名称 */
    char device_name[10];
    memset(device_name,0,10);
    sprintf((char*)frame_send.value.device_name, "kddt-%d", SELFID);        

    // 业务网口IP地址
    frame_send.value.service_ip[0]=192;
    frame_send.value.service_ip[1]=168;
    frame_send.value.service_ip[2]=2;
    frame_send.value.service_ip[3]=SELFID;

    frame_send.value.service_port = 6000;           // 业务网口端口号
    frame_send.value.serial_baudrate = 4;        // 串口波特率
    frame_send.value.serial_databits = 3;        // 串口数据位
    frame_send.value.serial_stopbits = 0;        // 串口停止位
    frame_send.value.serial_parity = 0;          // 串口校验位
    frame_send.value.serial_flowctrl = 0;        // 串口流控

    frame_send.value.gps_auto_sync=0;           // 位置设置-自动获取
    sprintf((char*)frame_send.value.longitude, "%.6f", gps_info_uart.lon);  // 经度
    sprintf((char*)frame_send.value.latitude, "%.6f", gps_info_uart.lat);   // 纬度
    frame_send.value.altitude = gps_info_uart.gaodu*10;               // 高度

    frame_send.value.time_auto_sync = 0;         // 时间自动获取
    frame_send.value.manual_hour = gps_info_uart.bj_time[0];            // 手动设置时
    frame_send.value.manual_minute = gps_info_uart.bj_time[1];          // 手动设置分
    frame_send.value.manual_second = gps_info_uart.bj_time[2];          // 手动设置秒
    frame_send.check = htons(CRC_Check(&frame_send.dirrection, BYTE05SIZE-6));                  //校验
    frame_send.tail = htons(0x5DD5);
    memcpy(send_byte_stream,&frame_send,BYTE05SIZE);

    write(fd,(void*)send_byte_stream,BYTE05SIZE);
    sleep(1);
    return 0;
}


/*
 * Send_0x06
 * - 功能: 发送 0x06 消息统计帧（网口包计数、语音包计数等）
 * - 参数: fd  - 串口/设备文件描述符
 *           buf - 指向 Info_0x06_Statistics 的指针
 * - 行为说明:
 *     - 如果传入的 info->stat_flag == 2: 表示清零基准，会读取当前网卡统计并保存为基准值（用于差分显示）
 *     - 计算发送/接收计数时会用当前值减去保存的基准值
 * - 说明: UI 端显示的是差分值（相对于最近的清零操作），此函数负责组帧并发送。
 */

int8_t Send_0x06(int fd,void* buf)
{
    uint16_t BYTE06SIZE=sizeof(Frame_06_byte);

    Frame_06_byte frame_send ;
    memset(&frame_send,0,BYTE06SIZE);

    uint8_t send_byte_stream[BYTE06SIZE] ;

    static uint16_t s_rx_packet=0;
    static uint16_t s_tx_packet=0; 
    Info_0x06_Statistics *info=(Info_0x06_Statistics*)buf;

    if(info->stat_flag==1)
    {
        return 0;
    }
    else if(info->stat_flag==2)   //清零
    {
        get_interface_stats(info);
        s_rx_packet=info->eth_rx_packets;
        s_tx_packet=info->eth_tx_packets;
        stat_info.stat_flag=0;
        
        stat_info.audio_rx_packets=stat_info.audio_tx_packets=0;
    }
    get_interface_stats(info);

    frame_send.head = htons(0xD55D);
    frame_send.dirrection = 0x06;
    frame_send.message_type = 0xFF;
    frame_send.value.eth_tx_cnt = info->eth_tx_packets-s_tx_packet;      // 以太网消息发送个数
    frame_send.value.eth_rx_cnt = info->eth_rx_packets-s_rx_packet;      // 以太网消息接收个数
    frame_send.value.voice_tx_cnt = info->audio_tx_packets;              // 模拟话音发送个数
    frame_send.value.voice_rx_cnt = info->audio_rx_packets;              // 模拟话音接收个数
    frame_send.value.total_tx_cnt = frame_send.value.eth_tx_cnt+frame_send.value.voice_tx_cnt;      // 总发送消息个数
    frame_send.value.total_rx_cnt = frame_send.value.eth_rx_cnt+frame_send.value.voice_rx_cnt;      // 总接收消息个数

    frame_send.check = htons(CRC_Check(&frame_send.dirrection, BYTE06SIZE-6));                  //校验
    frame_send.tail = htons(0x5DD5);

    memcpy(send_byte_stream,&frame_send,BYTE06SIZE);

    write(fd,(void*)send_byte_stream,BYTE06SIZE);
    sleep(1);
    return 0;
}

/*
 * Send_0x07
 * - 功能: 发送 0x07 设备硬件与状态汇报帧（电池、电源、温度、射频通道状态等）
 * - 参数: fd   - 串口/设备文件描述符
 *           info - 指向 DEVICE_SC_STATUS_REPORT 的指针（硬件状态采集结构）
 * - 说明: 该帧将设备的传感器/模块状态映射到 UI 预定义字段，便于前端监控设备健康状态。
 */
int8_t Send_0x07(int fd,void* info)
{
    Frame_07_byte frame_send ;
    int BYTE07SIZE=sizeof(Frame_07_byte);
    memset(&frame_send,0,BYTE07SIZE);

    uint8_t send_byte_stream[BYTE07SIZE] ;

    DEVICE_SC_STATUS_REPORT amp_param;
    memcpy(&amp_param,(DEVICE_SC_STATUS_REPORT*)info,sizeof(DEVICE_SC_STATUS_REPORT));
    
    frame_send.head = htons(0xD55D);
    frame_send.dirrection = 0x07;
    frame_send.message_type = 0xFF;
    frame_send.value.self_test_status = 0;                     // 自检状态
    frame_send.value.battery_remaining_capacity = amp_param.battery_level;           // 电池剩余电量
    frame_send.value.battery_cycle_count = 1000;                  // 电池剩余循环次数
    frame_send.value.battery_self_test_status = amp_param.battery_self_test;             // 电池自检状态
    frame_send.value.info_processor_temp = amp_param.temperature;                  // 综合信息处理温度
    frame_send.value.fan_speed_status = amp_param.fan_status;                     // 风机转速状态
    frame_send.value.nav_lock_status = amp_param.nav_lock_status;                      // 卫导锁定状态
    frame_send.value.clock_selection = amp_param.sync_status;                      // 时钟选择状态
    frame_send.value.adc_status = amp_param.sense_adc_status;                           // ADC状态
    frame_send.value.clock_source_temp = amp_param.freq_temperature;                    // 时钟频率源温度
    frame_send.value.freq_word_send_count = amp_param.freq_word_count;                 // 频率字下发次数计数
    frame_send.value.comm_sensing_status = amp_param.rf_sense_status;                  // 通信感知状态
    frame_send.value.ref_clock1_status = 0x01;                    // 输出参考时钟1状态
    frame_send.value.ref_clock2_status = 0x01;                    // 输出参考时钟2状态
    frame_send.value.lo1_output_status = (amp_param.freq_lo_ready >> 0) & 0x01;                    // 本振1输出状态
    frame_send.value.lo2_output_status = (amp_param.freq_lo_ready >> 1) & 0x01;                    // 本振2输出状态
    frame_send.value.lo3_output_status = (amp_param.freq_lo_ready >> 2) & 0x01;                    // 本振3输出状态
    frame_send.value.lo4_output_status = (amp_param.freq_lo_ready >> 3) & 0x01;                    // 本振4输出状态
    frame_send.value.power_conversion_temp = amp_param.power_temperature;                // 电源变换温度
    frame_send.value.power_on_fault_indicator = amp_param.power_power_fault;             // 上电/故障指示
    frame_send.value.power_supply_control = amp_param.power_ch_power_status;                 // 各路加电控制
    frame_send.value.ac220_power_consumption = amp_param.power_ac220_power;              // AC220功耗
    frame_send.value.dc24v_power_consumption = amp_param.power_dc24v_power;              // DC24V功耗

    frame_send.value.rf_channel_temp = amp_param.rf_ch1_temp1;                          // 通道1温度
    frame_send.value.rf_channel_tx_power_status = amp_param.rf_tx_power_status;               // 发射功率检波状态
    frame_send.value.rf_channel_antenna_l_vswr = amp_param.rf_antenna_l_vswr;                // 天线L口驻波状态
    frame_send.value.rf_channel_antenna_h1_vswr = amp_param.rf_antenna_h1_vswr;               // 天线H-1口驻波状态
    frame_send.value.rf_channel_antenna_h2_vswr =  amp_param.rf_antenna_h2_vswr;               // 天线H-2口驻波状态
    frame_send.value.rf_channel_tx_rx_status = amp_param.rf_tx_rx_status;                  // 收发状态指示
    frame_send.value.rf_Channel_antenna_select = amp_param.rf_antenna_select;                // 天线选择控制状态
    frame_send.value.rf_channel_comm_sensing = amp_param.rf_sense_status;                  // 通信感知状态
    frame_send.value.rf_channel_power_control = amp_param.power_ch_power_status;                 // 加电控制状态
    frame_send.value.rf_channel_power_level = amp_param.rf_ch_power_level;                   // 通道功率等级

    frame_send.Channel1.rf_channel_rf_power_detect = amp_param.rf_ch1_rf_power;               // 射频功率检波
    frame_send.Channel1.rf_channel_if_power_detect = amp_param.rf_ch1_if_power;               // 中频功率检波
    frame_send.Channel1.rf_channel_current_freq = amp_param.rf_ch1_freq;                  // 当前频点
    frame_send.Channel1.rf_channel_agc_attenuation = amp_param.rf_ch1_agc_atten;               // AGc衰减值
    frame_send.Channel1.rf_channel_signal_bandwidth = amp_param.rf_ch1_bandwidth;              // 通道1信号带宽
    frame_send.Channel1.rf_channel_attenuation = amp_param.rf_ch1_agc_atten;                   // 通道1衰减量

    frame_send.Channel2.rf_channel_rf_power_detect = amp_param.rf_ch2_rf_power;               // 射频功率检波
    frame_send.Channel2.rf_channel_if_power_detect = amp_param.rf_ch2_if_power;               // 中频功率检波
    frame_send.Channel2.rf_channel_current_freq = amp_param.rf_ch2_freq;                  // 当前频点
    frame_send.Channel2.rf_channel_agc_attenuation = amp_param.rf_ch2_agc_atten;               // AGc衰减值
    frame_send.Channel2.rf_channel_signal_bandwidth = amp_param.rf_ch2_bandwidth;              // 通道1信号带宽
    frame_send.Channel2.rf_channel_attenuation = amp_param.rf_ch2_agc_atten;                   // 通道1衰减量

    frame_send.Channel3.rf_channel_rf_power_detect = amp_param.rf_ch3_rf_power;               // 射频功率检波
    frame_send.Channel3.rf_channel_if_power_detect = amp_param.rf_ch3_if_power;               // 中频功率检波
    frame_send.Channel3.rf_channel_current_freq = amp_param.rf_ch3_freq;                  // 当前频点
    frame_send.Channel3.rf_channel_agc_attenuation = amp_param.rf_ch3_agc_atten;               // AGc衰减值
    frame_send.Channel3.rf_channel_signal_bandwidth = amp_param.rf_ch3_bandwidth;              // 通道1信号带宽
    frame_send.Channel3.rf_channel_attenuation = amp_param.rf_ch3_agc_atten;                   // 通道1衰减量

    frame_send.Channel4.rf_channel_rf_power_detect = amp_param.rf_ch4_rf_power;               // 射频功率检波
    frame_send.Channel4.rf_channel_if_power_detect = amp_param.rf_ch4_if_power;               // 中频功率检波
    frame_send.Channel4.rf_channel_current_freq = amp_param.rf_ch4_freq;                  // 当前频点
    frame_send.Channel4.rf_channel_agc_attenuation = amp_param.rf_ch4_agc_atten;               // AGc衰减值
    frame_send.Channel4.rf_channel_signal_bandwidth = amp_param.rf_ch4_bandwidth;              // 通道1信号带宽
    frame_send.Channel4.rf_channel_attenuation = amp_param.rf_ch4_agc_atten;                   // 通道1衰减量

    frame_send.check = htons(CRC_Check(&frame_send.dirrection, BYTE07SIZE-6));                  //校验
    frame_send.tail = htons(0x5DD5);
    memcpy(send_byte_stream,&frame_send,BYTE07SIZE);
    
    
    write(fd,(void*)send_byte_stream,BYTE07SIZE);
    sleep(1);
    return 0;
}

/*
 * Send_0x08
 * - 功能: 周期性发送节点参数（如工作模式、信道、带宽、mcs、sync_mode 等）给 UI
 * - 参数: fd   - 串口/设备文件描述符
 *           info - 指向 Node_Xwg_Pairs 数组（/etc/node_xwg 解析结果）
 *           size - info 的字节长度
 * - 帧中重要字段说明:
 *     center_freq/select_freq1..4 - 频点（均以 kHz 表示，代码中乘以 1000）
 *     bw - 带宽（对应 node_xwg 中 bw）
 *     mcs - 调制/速率编码
 * - 说明: UI 会根据此帧展示当前节点配置，供用户查看或触发修改。
 */
int8_t Send_0x08(int fd,void* info,int size)
{
    Frame_08_byte frame_send ;
    memset(&frame_send,0,sizeof(Frame_08_byte));
    int BYTE08SIZE=sizeof(Frame_08_byte);

    
    uint8_t send_byte_stream[BYTE08SIZE] ;
    uint8_t byte_stream_recv[9] ;

    Node_Xwg_Pairs *param_pairs=(Node_Xwg_Pairs *)malloc(size);
    memcpy(param_pairs,(Node_Xwg_Pairs*)info,size);


    frame_send.head = htons(0xD55D);
    frame_send.dirrection = 0x08;
    frame_send.message_type = 0xFF;
    memcpy(frame_send.current_time,gps_info_uart.bj_time,3) ;
    frame_send.net_join_state=0;
    
    frame_send.kylb = 0x01;         // 空域滤波 0：开启 1：关闭
    if(get_int_value((void*)param_pairs,"kylb")==KYLB_MODE_OPEN)
    {
        frame_send.kylb = 0;     
    }

    if(get_int_value((void*)param_pairs,"workmode")==WORK_MODE_TYPE_DP)
    {
        frame_send.workmode = 0x00; // 跳频方式 定频
    }
    if(get_int_value((void*)param_pairs,"workmode")==WORK_MODE_TYPE_ZSYXP)
    {
        frame_send.workmode = 1;      // 跳频方式 自适应选频
    }

    frame_send.center_freq    = get_int_value((void*)param_pairs,"channel")*1000;// 中心频率（点频） *1000
    frame_send.select_freq1  = get_int_value((void*)param_pairs,"select_freq1")*1000;// 自适应-中心频率1 *1000
    frame_send.select_freq2  = get_int_value((void*)param_pairs,"select_freq2")*1000;// 自适应-中心频率2 *1000
    frame_send.select_freq3  = get_int_value((void*)param_pairs,"select_freq3")*1000;// 自适应-中心频率3 *1000
    frame_send.select_freq4  = get_int_value((void*)param_pairs,"select_freq4")*1000;// 自适应-中心频率4 *1000
    
    frame_send.sync_mode =  (uint8_t)get_int_value((void*)param_pairs,"sync_mode");

    frame_send.bw = (uint8_t)get_int_value((void*)param_pairs,"bw");        // 信号带宽

    frame_send.mcs =(uint8_t)get_int_value((void*)param_pairs,"mcs");

    frame_send.tx_power_spread = 0;// 发射功率
    frame_send.tx_attenuation = 0;// 发射功率衰减

    frame_send.check = htons(CRC_Check(&frame_send.dirrection, BYTE08SIZE-6));                  //校验
    frame_send.tail = htons(0x5DD5);
    memcpy(send_byte_stream,&frame_send,BYTE08SIZE);

    write(fd,(void*)send_byte_stream,BYTE08SIZE);
    sleep(1);
    free(param_pairs);
    return 0;
}

/*
 * Send_0x09
 * - 功能: 发送 0x09 网络邻居状态帧（邻居节点 ID、IP、跳数、信号强度、时延）
 * - 参数: fd   - 串口/设备文件描述符
 *           info - 指向 struct mgmt_send 的指针（包含邻居列表与统计数据）
 * - 说明: 该函数把邻居数组序列化成帧并计算 CRC 写入串口，UI 端用于展示拓扑邻居列表。
 */
int8_t Send_0x09(int fd,void* info)
{
    if (fd < 0 || info == NULL) {
        return -1;
    }

    int neigh_num=0;
    int size=0;

    int NODESTATUSSIZE = sizeof(NetworkNodeStatus);
    int BYTE0ASIZE=sizeof(Frame_0A_byte);

    struct mgmt_send self_msg;

    memcpy((void*)&self_msg,(struct mgmt_send*)info,sizeof(self_msg));

    neigh_num=self_msg.neigh_num;

    if(neigh_num==0||neigh_num>4)
    {
        return 0;
    }
    size=NODESTATUSSIZE*neigh_num;

    NetworkNodeStatus *net_info=(NetworkNodeStatus*)malloc(size);
    memset(net_info,0,size);
    uint8_t send_byte_stream[size+9] ;

    for(int idx=0; idx<neigh_num; idx++)
    {
        net_info[idx].member_id=self_msg.msg[idx].node_id;
        net_info[idx].ip_address[0]=192;
        net_info[idx].ip_address[1]=168;
        net_info[idx].ip_address[2]=2;
        net_info[idx].ip_address[3]=self_msg.msg[idx].node_id;
    
        net_info[idx].hop_count=1;
        net_info[idx].signal_strength=0-self_msg.msg[idx].rssi;
        net_info[idx].transmission_delay=self_msg.msg[idx].time_jitter;

    }

    send_byte_stream[0]=0xD5;       
    send_byte_stream[1]=0x5D;       
    send_byte_stream[2]=0x09;       
    send_byte_stream[3]=0xFF;       
    send_byte_stream[4]=neigh_num;      
     
    memcpy(send_byte_stream+5,net_info,size);
    uint16_t check = CRC_Check(send_byte_stream+2, size+3);
    send_byte_stream[size+5]=check >> 8;
    send_byte_stream[size+6]=check & 0xff;
    send_byte_stream[size+7]=0x5d;
    send_byte_stream[size+8]=0xd5;


    int cmd_len=write(fd,(void*)send_byte_stream,size+9);
    free(net_info);

    sleep(3);
    return 0;
}

/*
 * Send_0x0A
 * - 功能: 发送 0x0A 单个节点的详细信息回复（用于回复网内节点查询请求）
 * - 参数: fd    - 串口/设备文件描述符
 *           param - 指向 MEM_REPLY_FRAME（或 NodeBasicInfo 包含结构）的指针
 *           size  - param 的字节长度
 * - 说明: 上层在收到成员回复或需要主动上报时调用该函数进行帧封装。
 */
int8_t Send_0x0A(int fd,void* param,int size)
{
    if (fd < 0 || param == NULL || size <= 0) {
        return -1;
    }
    uint8_t i=0;
    Frame_0A_byte frame_0A;
    int BYTE0ASIZE=sizeof(Frame_0A_byte);
    memset(&frame_0A,0,BYTE0ASIZE);
    uint8_t send_byte_stream[BYTE0ASIZE] ;

    Node_Xwg_Pairs *param_pairs=(Node_Xwg_Pairs *)malloc(size);
    if (!param_pairs) {
        return -1;
    }
    memcpy(param_pairs,(Node_Xwg_Pairs*)param,size);

    MEM_REPLY_FRAME *member_info=(MEM_REPLY_FRAME*)param;
    

    frame_0A.head=htons(0xD55D);
    frame_0A.dirrection=0x0A;
    frame_0A.message_type=0x0;

    memcpy((void*)&frame_0A.member,(void*)&member_info->member,sizeof(NodeBasicInfo));


    frame_0A.check = htons(CRC_Check(&frame_0A.dirrection, BYTE0ASIZE-6));  
    frame_0A.tail =  htons(0x5DD5);

    memcpy(send_byte_stream,&frame_0A,BYTE0ASIZE);
    printf("send cmd 0x0a,len:%d\r\n",write(fd,(void*)send_byte_stream,BYTE0ASIZE));
        free(param_pairs);
    
    sleep(1);

    for(i=0;i<sizeof(send_byte_stream);i++)
    {
        printf("%02X",send_byte_stream[i]);
    }
    printf("\r\n"); 
    
    return 0;
}

/* 处理参数内容 配置电台参数 */
/*
 * process_cmd_info
 * - 功能: 处理来自 UI 的配置命令（通过地址 + 值），把 UI 的操作转为内部 mgmt_netlink 的设置
 * - 参数: cmd_addr  - 命令地址（参见 enum_uartparam_addr.h 中的 PARAM_* 宏）
 *           cmd_value - 命令对应的值（可为频率/模式/带宽等）
 * - 行为要点:
 *     - 根据 cmd_addr 做分支处理（如设置工作模式、带宽、频点、路由等）
 *     - 对会影响设备运行的改动，通过 mgmt_netlink_set_param 下发到内核模块
 *     - 同时更新 sqlite 数据库中对应的 systemInfo/meshInfo（updateData_systeminfo_qk）
 * - 说明: 这是 UI->系统的桥梁，负责把文本/数值映射为设备可识别的控制命令。
 */
void process_cmd_info(uint32_t cmd_addr, uint32_t cmd_value)
{
    int ret=0;
    uint32_t t_freq=0;
    
    static uint8_t uart_mcs_code=0;    //编码效率
    bool isset =FALSE;
    char bcast_buf[1000];
    uint8_t cmd[200];

    INT8 buffer[sizeof(Smgmt_header) + sizeof(Smgmt_set_param)];
    INT32 buflen = sizeof(Smgmt_header) + sizeof(Smgmt_set_param);
    Smgmt_header* mhead = (Smgmt_header*)buffer;
    Smgmt_set_param* mparam = (Smgmt_set_param*)mhead->mgmt_data;

    bzero(buffer, buflen);
    memset(cmd,0,sizeof(cmd));
    mhead->mgmt_head = htons(HEAD);
    mhead->mgmt_len = sizeof(Smgmt_set_param);
    mhead->mgmt_type = 0;

    /*static wg param init*/
    static uint8_t s_sync_mode=0;
    static uint8_t s_workmode=WORK_MODE_TYPE_DP;
    static uint8_t s_kylb=1;   //默认关闭
    static uint8_t s_bw=0;
    static uint8_t s_mcs=0;   //调制方式
    static uint8_t s_transmode=0;
    static uint32_t s_freq,s_select_freq1,s_select_freq2,s_select_freq3,s_select_freq4;
    s_freq=FREQ_INIT;

    switch (cmd_addr)
    {

    case PARAM_0A_REQUEST_ADDR:
        send_member_request((uint8_t)cmd_value);
    break;
    case PARAM_OP_MODE_CURRENT_WORK_MODE:
        printf("[UI DEBUG] transmode :%d",cmd_value);
        isset=TRUE;
        mhead->mgmt_type |= MGMT_SET_TEST_MODE;
        mparam->mgmt_mac_work_mode=htons((uint16_t)cmd_value);
        s_transmode=cmd_value;
    break;
    case PARAM_OP_MODE_SPATIAL_FILTERING:     //空域滤波
        printf("[UI DEBUG] kylb :%d \r\n", (uint8_t)cmd_value);
        s_kylb=cmd_value;
        if(cmd_value==0)
        {
            if(s_workmode==WORK_MODE_TYPE_ZSYXP)
            {
                printf("DT workmode is zsyxp,fail to set kylb\r\n");
                break;
            }
            isset=TRUE;
            mhead->mgmt_type |= MGMT_SET_WORKMODE;
            mparam->mgmt_net_work_mode.NET_work_mode=WORK_MODE_TYPE_KYLB;
            
            sprintf((char*)cmd,
                "sed -i \"s/kylb .*/kylb %d/g\" /etc/node_xwg",
                KYLB_MODE_OPEN);            
            system((char*)cmd);
        }
        else
        {
            sprintf((char*)cmd,
                "sed -i \"s/kylb .*/kylb %d/g\" /etc/node_xwg",
                KYLB_MODE_CLOSE);           
            system((char*)cmd);
        }  // <====== 【修复点】：这里补上了确实的大括号！
        break;

    case PARAM_CH1_FREQ_HOPPING_MODE:  //跳频方式
        printf("[UI DEBUG] work mode: %d \r\n",cmd_value);
        if(cmd_value==0)
        {
            printf("[UI DEBUG]set work mode dp \r\n ");
            isset=TRUE;
            mhead->mgmt_type |= MGMT_SET_WORKMODE;
            mparam->mgmt_net_work_mode.NET_work_mode=WORK_MODE_TYPE_DP;
            mhead->mgmt_type |= MGMT_SET_FREQUENCY;
            mparam->mgmt_mac_freq = htonl(s_freq);

            s_workmode=WORK_MODE_TYPE_DP;

            sprintf((char*)cmd,
                "sed -i \"s/workmode .*/workmode %d/g\" /etc/node_xwg",
                WORK_MODE_TYPE_DP);             
            system((char*)cmd);       
        }
        else if(cmd_value==1)
        {
            /*自适应选频*/
            if(s_select_freq1==0||s_select_freq2==0||s_select_freq3==0||s_select_freq4==0)
            {
                printf("[UI DEBUG] set zsyxp error , freq info :%d %d %d %d \r\n",s_select_freq1,
                s_select_freq2,s_select_freq3,s_select_freq4);
                break;
            }
            printf("[UI DEBUG]set work mode zsyxp \r\n ");
            isset=TRUE;
            mhead->mgmt_type |= MGMT_SET_WORKMODE;
            mparam->mgmt_net_work_mode.NET_work_mode=WORK_MODE_TYPE_ZSYXP;

            mparam->mgmt_net_work_mode.fh_len=4;
            mparam->mgmt_net_work_mode.hop_freq_tb[0]=s_select_freq1;
            mparam->mgmt_net_work_mode.hop_freq_tb[1]=s_select_freq2;
            mparam->mgmt_net_work_mode.hop_freq_tb[2]=s_select_freq3;
            mparam->mgmt_net_work_mode.hop_freq_tb[3]=s_select_freq4;


            s_workmode=WORK_MODE_TYPE_ZSYXP;
            sprintf((char*)cmd, "sed -i \"s/workmode .*/workmode %d/; \
                        s/select_freq1 .*/select_freq1 %d/; \
                        s/select_freq2 .*/select_freq2 %d/; \
                        s/select_freq3 .*/select_freq3 %d/; \
                        s/select_freq4 .*/select_freq4 %d/\" /etc/node_xwg", 
                    WORK_MODE_TYPE_ZSYXP, s_select_freq1, s_select_freq2, s_select_freq3, s_select_freq4);
                system((char*)cmd);
        }
    break;

    case PARAM_CH1_ROUTING_PROTOCOL:    //路由协议
        printf("[UI DEBUG] router :%d \r\n", (uint8_t)cmd_value);    
        switch(cmd_value)
        {
            case 0:   //olsr
                printf("[UI DEBUG] set route olsr \r\n");
                ret = system("/home/root/cs_olsr.sh");//切换路由协议脚本
                if(ret == -1) printf("change olsr failed\r\n");
                sprintf((char*)cmd,
                    "sed -i \"s/router .*/router %d/g\" /etc/node_xwg",
                KD_ROUTING_OLSR);       
                system((char*)cmd);
            break;
            case 1:  //aodv
                printf("[UI DEBUG] set route aodv \r\n");
                ret = system("/home/root/cs_aodv.sh");
                if(ret == -1) printf("change batman failed\r\n");

                sprintf((char*)cmd,
                    "sed -i \"s/router .*/router %d/g\" /etc/node_xwg",
                KD_ROUTING_AODV);       
                system((char*)cmd);
            break;
            case 2:  //batman 
                printf("[UI DEBUG] set route batman \r\n");
                ret = system("/home/root/cs_batman.sh");
                if(ret == -1) printf("change batman failed\r\n");
                sprintf((char*)cmd,
                    "sed -i \"s/router .*/router %d/g\" /etc/node_xwg",
                KD_ROUTING_CROSS_LAYER);        
                system((char*)cmd);
            break;
            default:
            break;
        }
        break;
    case PARAM_CH1_FIXED_FREQ_CENTER:    //定频-中心频点
        printf("[UI DEBUG]fix freq :%d workmode :%d \r\n", cmd_value/1000,s_workmode);
        s_freq=t_freq=cmd_value/1000;
        if(t_freq<=225) s_freq=225;
        if(t_freq>=2500) s_freq=2500;
        if(s_workmode==WORK_MODE_TYPE_ZSYXP)
        {
            break;
        }
        isset=TRUE;
        mhead->mgmt_type |= MGMT_SET_WORKMODE;
        mparam->mgmt_net_work_mode.NET_work_mode =WORK_MODE_TYPE_DP;
        mhead->mgmt_type |= MGMT_SET_FREQUENCY;
        mparam->mgmt_mac_freq = htonl(s_freq);

        updateData_systeminfo_qk("rf_freq",s_freq);
        break;

    case PARAM_CH1_SELECTED_FREQ_1:
        printf("[UI DEBUG]select freq-1 :%d \r\n", cmd_value/1000);
        t_freq=cmd_value/1000;
        s_select_freq1=t_freq;
        if(t_freq<=225) s_select_freq1=225;
        if(t_freq>=2500) s_select_freq1=2500;
    break;
    case PARAM_CH1_SELECTED_FREQ_2:
        printf("[UI DEBUG]select freq-2 :%d  \r\n", cmd_value/1000);
        t_freq=cmd_value/1000;
        s_select_freq2=t_freq;
        if(t_freq<=225) s_select_freq2=225;
        if(t_freq>=2500) s_select_freq2=2500;
    break;
    case PARAM_CH1_SELECTED_FREQ_3:
        printf("[UI DEBUG]select freq-3 :%d \r\n", cmd_value/1000);
        t_freq=cmd_value/1000;
        s_select_freq3=t_freq;
        if(t_freq<=225) s_select_freq3=225;
        if(t_freq>=2500) s_select_freq3=2500;
    break;
    case PARAM_CH1_SELECTED_FREQ_4:
        printf("[UI DEBUG]select freq-4 :%d \r\n", cmd_value/1000);
        t_freq=cmd_value/1000;
        s_select_freq4=t_freq;
        if(t_freq<=225) s_select_freq4=225;
        if(t_freq>=2500) s_select_freq4=2500;
    break;      

    case PARAM_CH1_SIGNAL_BANDWIDTH:    //带宽
        printf("[UI DEBUG]bw: %d \r\n",cmd_value);
        isset=TRUE;
        mhead->mgmt_type |= MGMT_SET_BANDWIDTH;
        mparam->mgmt_mac_bw=cmd_value;
        
        updateData_systeminfo_qk("m_chanbw",mparam->mgmt_mac_bw);
        break;
    case PARAM_CH1_MODULATION_WIDEBAND:    //宽带 调制方式
        printf("[UI DEBUG] mcs: %d \r\n",cmd_value);
        isset=TRUE;
        mhead->mgmt_type |= MGMT_SET_UNICAST_MCS;

        s_mcs=cmd_value;
        mparam->mgmt_virt_unicast_mcs=s_mcs;
        updateData_systeminfo_qk("m_rate",mparam->mgmt_virt_unicast_mcs);
        break;
    case PARAM_OP_MODE_SYNC_MODE:
        s_sync_mode=cmd_value;
        sprintf((char*)cmd,
            "sed -i \"s/sync_mode .*/sync_mode %d/g\" /etc/node_xwg",
            cmd_value);     
        system((char*)cmd);
        break;

    case PARAM_CH1_TX_POWER_ATTENUATION:    //发射功率衰减
        printf("[UI DEBUG] power_atten: %d \r\n",cmd_value);

        break;
    case PARAM_TXRX_INFO_OPERATION:     //消息统计操作
        printf("[UI DEBUG] 0x06 cmd info operation: %d\r\n",cmd_value);
        stat_info.stat_flag=cmd_value;

        break;  
    default:
        break;
    }

    if(isset)
    {
        isset=FALSE;
        mhead->mgmt_type = htons(mhead->mgmt_type);
        mhead->mgmt_keep = htons(mhead->mgmt_keep);
        mgmt_netlink_set_param(buffer, buflen,NULL);    
        sleep(1);
        if (!persist_test_db()) {
            printf("[ui_get] persist test.db failed after UI command\n");
        }
    }
}

/* 处理工控屏指令 */
/*
 * reply_uart_info
 * - 功能: 将应答数据直接写回到串口（用于 ACK 应答）
 * - 参数: fd - 串口文件描述符
 *           ack_info - 指向应答帧的缓冲区
 *           len - 应答帧长度（字节）
 * - 返回: 实际写入字节数
 */
uint16_t  reply_uart_info(int fd,void* ack_info, uint16_t len)
{
    return write(fd,ack_info,len);
}

/*
 * process_uart_info
 * - 功能: 解析从串口接收到的原始帧并分发处理
 * - 参数: fd   - 串口文件描述符
 *           info - 接收到的原始字节缓冲区
 *           len  - 缓冲区长度
 * - 关键点:
 *     - 验证帧头(0xD5 0x5D) 和 帧尾(0x5D 0xD5)
 *     - 解析命令字、参数长度、参数地址，并根据参数长度调用 process_cmd_info
 *     - 对命令进行 ACK 回复（构造对应长度的 ACK 帧并写回串口）
 */
void process_uart_info(int fd,char* info, int len)
{

    uint8_t uart_buf[MAX_UI_SIZE];
    uint8_t cmd_type = 0;
    uint16_t cmd_len = 0;
    uint8_t  param_len = 0;

    UART_FRAME_1_BYTE recv_frame_1;
    UART_FRAME_2_BYTE recv_frame_2;
    UART_FRAME_4_BYTE recv_frame_4;
    UART_FRAME_16_BYTE recv_frame_16;

    memset(&recv_frame_1,0,sizeof(UART_FRAME_1_BYTE));
    memset(&recv_frame_2,0,sizeof(UART_FRAME_2_BYTE));
    memset(&recv_frame_4,0,sizeof(UART_FRAME_4_BYTE));
    memset(&recv_frame_16,0,sizeof(UART_FRAME_16_BYTE));

    if (info == NULL || len <= 0)
    {
        printf("ERROR:uart info is null\r\n");
        return;
    }

    memset(uart_buf, 0, MAX_UI_SIZE);
    memcpy(uart_buf, info, len);
    
    if (uart_buf[0] != 0xd5 || uart_buf[1] != 0x5d)//帧头校验
    {
        printf("ERROR:uart head error\r\n");
        return;
    }

    if (uart_buf[len - 2] != 0x5d || uart_buf[len - 1] != 0xd5)//帧尾校验
    {
        printf("ERROR:uart tail error\r\n");
        return;

    }

    cmd_type = uart_buf[2];   //命令字

    if(cmd_type==0x0a)
    {
        /* 处理0a命令 网内节点详细信息查询 */
        process_cmd_info(PARAM_0A_REQUEST_ADDR,uart_buf[4]);
        return;
    }

    cmd_len = uart_buf[4];   //参数数值长度
    uint32_t addr = (uart_buf[5] << 24) | (uart_buf[6] << 16) | (uart_buf[7] << 8) | (uart_buf[8]);
    addr = htonl(addr);
    param_len = cmd_len - 1 - 4;//参数长度 = 总长度 - 命令字长度(1) - 地址长度(4)

    switch (param_len)
    {
        case 1:
            memcpy(&recv_frame_1, info, len);
            process_cmd_info(addr, recv_frame_1.value);
            //ack
            recv_frame_1.cmd_no = 0x02;
            recv_frame_1.ack_flag = MESSAGE_TYPE_REPLY;
            recv_frame_1.crc = htons(CRC_Check(&recv_frame_1.cmd_no, sizeof(recv_frame_1)-6));                  //校验
            reply_uart_info(fd,(void*)&recv_frame_1,sizeof(recv_frame_1));

            break;
        case 2:
            memcpy(&recv_frame_2, info, len);
            process_cmd_info(addr, recv_frame_2.value);

            recv_frame_2.cmd_no = 0x02;
            recv_frame_2.ack_flag = MESSAGE_TYPE_REPLY;
            recv_frame_2.crc = htons(CRC_Check(&recv_frame_2.cmd_no, sizeof(recv_frame_2)-6));                  //校验
            reply_uart_info(fd,(void*)&recv_frame_2,sizeof(recv_frame_2));

            break;
        case 4:
            memcpy(&recv_frame_4, info, len);
            process_cmd_info(addr, recv_frame_4.value);

            //ack
            recv_frame_4.cmd_no = 0x02;
            recv_frame_4.ack_flag = MESSAGE_TYPE_REPLY;
            recv_frame_4.crc = htons(CRC_Check(&recv_frame_4.cmd_no, sizeof(recv_frame_4)-6));                  //校验

            reply_uart_info(fd,(void*)&recv_frame_4,sizeof(recv_frame_4));
        default:
            break;
    }
}

/*
 * read_xwg_info
 * - 功能: 读取 /etc/node_xwg 中的本节点配置并填充到 NodeBasicInfo 结构
 * - 参数: info - 输出缓冲区，拷贝 NodeBasicInfo 到此处
 *           size - info 缓冲区长度
 * - 说明: 该函数依赖 read_node_xwg_file 来解析 key/value 对，并将关键字段映射到 node_info
 */
void read_xwg_info(char *info,int size)
{
/* temp param */
    uint8_t  t_workmode=0;
    uint32_t t_freq;
    uint8_t  t_bw;
    uint8_t  t_mcs;
    uint8_t  t_routing;
    uint8_t  t_kylb;
    
// read /etc/node_xwg
    Node_Xwg_Pairs param_pairs[] = {
        {"channel", 0, 0, 0},{"power", 0, 0, 0},{"bw", 0, 0, 0},{"mcs", 0, 0, 0},
        {"macmode", 0, 0, 0},{"slotlen", 0, 0, 0},{"router", 0, 0, 0},{"workmode", 0, 0, 0},
        {"select_freq1", 0, 0, 0},{"select_freq2", 0, 0, 0},{"select_freq3", 0, 0, 0},{"select_freq4", 0, 0, 0},
        {"sync_mode", 0, 0, 0},{"kylb", 0, 0, 0}
    };

    read_node_xwg_file("/etc/node_xwg",param_pairs,MAX_XWG_PAIRS);

    NodeBasicInfo node_info;
    memset(&node_info,0,sizeof(NodeBasicInfo));

    node_info.member_id=SELFID;
    node_info.ip_address[0]=192;
    node_info.ip_address[1]=168;
    node_info.ip_address[2]=2;
    node_info.ip_address[3]=SELFID;

    t_workmode=(uint8_t)get_int_value((void*)param_pairs,"workmode");
    t_kylb=(uint8_t )get_int_value((void*)param_pairs,"kylb");
    t_freq=get_int_value((void*)param_pairs,"channel")*1000;
    
    t_bw=(uint8_t)get_int_value((void*)param_pairs,"bw");
    t_mcs=(uint8_t)get_int_value((void*)param_pairs,"mcs");
    t_routing=(uint8_t)get_int_value((void*)param_pairs,"router");

    node_info.spatial_filter_status=1;  //默认空余滤波关
    node_info.channel1.ch_frequency_hopping=0; //默认定频

    if(t_kylb==KYLB_MODE_OPEN)
    {
        node_info.spatial_filter_status=0;
    }
    if(t_workmode==5)
    {
        node_info.channel1.ch_frequency_hopping=1;
    }

    node_info.channel1.ch_working_freq=t_freq*1000;
    node_info.channel1.ch_waveform=t_mcs;
    node_info.channel1.ch_signal_bandwidth=t_bw;
    node_info.channel1.ch_routing_protocol=t_routing-1;
    
    memcpy(info,(void*)&node_info,size);
}


/*
 * send_member_request
 * - 功能: 向指定节点发起成员信息请求（单播 UDP）
 * - 参数: id - 目标节点 ID（会被映射为 192.168.2.<id>）
 * - 行为: 构造 MEM_REQUEST_FRAME 并通过 UDP 发送到目标的 MEM_REQUEST_PORT
 */
/* 处理网内成员信息单播包 */
void send_member_request(uint8_t id)
{
    int s_request;
    int ret=0;
    char dest_ip[20];

    memset(dest_ip,0,sizeof(dest_ip));
    snprintf(dest_ip, sizeof(dest_ip), "192.168.2.%d", id);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr)); 
    s_request=createUdpClient(&addr,dest_ip,MEM_REQUEST_PORT);

    MEM_REQUEST_FRAME info;
    memset(&info,0,sizeof(MEM_REQUEST_FRAME));
    info.head=htons(0x4c4a);  // 局域网通信的“暗号”头
    info.src_id=SELFID;       // 告诉对方：我是 1 号
    info.dst_id=id;           // 告诉对方：我要找 5 号
    info.type=0;              // 【核心】type=0 代表“这是一个提问（Request）”
    info.tail=htons(0x6467);  // 局域网通信的“暗号”尾

    ret=SendUDPClient(s_request,(char*)&info,sizeof(MEM_REQUEST_FRAME),&addr);
    if(ret<=0)
    {
        printf("[UI DEBUG]send member request fail\r\n");
    }
    printf("send member request \r\n");
    sleep(1);
    close(s_request);
}


/*
 * process_member_info
 * - 功能: 处理收到的网内成员单播包（MEM_REQUEST_FRAME）
 * - 参数: info - UDP 接收的原始数据缓冲
 *           size - 缓冲区长度
 * - 行为要点:
 *     - 校验帧头(head)
 *     - 若 type==0: 本节点构造 MEM_REPLY_FRAME 回复请求者
 *     - 若 type==1: 表示收到别人回复，将该回复上报给 UI（Send_0x0A）
 */

void process_member_info(char* info,int size)
{
    if(info==NULL||size<=0)
    {
        return;
    }

    int reply_s;
    int ret=0;
    char dest_ip[20];
    memset(dest_ip,0,sizeof(dest_ip));

    
    MEM_REQUEST_FRAME *request_info=(MEM_REQUEST_FRAME*)info;

    if(ntohs(request_info->head)!=0x4c4a)
    {
        printf("[ERROR] member request head error\r\n");
        return;
    }

    MEM_REPLY_FRAME reply_info;
    memset(&reply_info,0,sizeof(MEM_REPLY_FRAME));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr)); 


    switch(request_info->type)
    {
        case 0:
        /* 回复成员信息单播请求 */

            printf("recv member request info\r\n");
            reply_info.head=htons(0x4c4a);
            reply_info.type=1;
            reply_info.src_id=SELFID;
            reply_info.dst_id=request_info->src_id;
            reply_info.tail=htons(0x6467);

            read_xwg_info((void*)&reply_info.member,sizeof(NodeBasicInfo));

            snprintf(dest_ip, sizeof(dest_ip), "192.168.2.%d", request_info->src_id);

            reply_s=createUdpClient(&addr,dest_ip,MEM_REQUEST_PORT);
            ret=SendUDPClient(reply_s,(char*)&reply_info,sizeof(MEM_REPLY_FRAME),&addr);
            if(ret<=0)
            {
                printf("[ERROR]send member reply info fail\r\n");
            }
            sleep(1);
            close(reply_s);
        break;
        case 1:
        /* 收到回复，直接上报0x0A */
            printf("recv member reply info\r\n");
            memcpy(&reply_info,info,size);
            //增加帧头帧尾判断
            Send_0x0A(ui_fd,(void*)info,size);

        break;
        
        default:
        break;
    }

}

/* 初始化工控屏 */
/*
 * uart_init
 * - 功能: 打开并配置工控屏串口，返回串口文件描述符
 * - 返回: >=0 的 fd 表示成功，-1 表示失败
 */
int uart_init(void)
{
    int ui_Fd;
    uint8_t cnt=0;

    while(cnt<MAX_RETRY_COUNT)
    {
        ui_Fd = open(FD_UI_UART, O_RDWR);  //打开串口屏串口   |O_NOCTTY
        if(ui_Fd!=-1)
            break;
        cnt++;
        if(cnt<MAX_RETRY_COUNT)
            sleep(1);       
    }
    if(ui_Fd==-1)
    {
        printf("ERROR:open ui uart fail\r\n");
        return -1;
    }
    printf("[UI DEBUG]uart fd : %d \r\n",ui_Fd);
     set_opt(ui_Fd, UI_UART_BAUD, 8, 'N', 1);   //设置nmea串口属性
    return ui_Fd;
}


/*
 * get_ui_info
 * - 功能: 从串口循环读取数据并交给 process_uart_info 解析
 * - 参数: fd - 串口文件描述符
 * - 说明: 该函数为阻塞读取循环，通常在独立线程中运行。
 */
void get_ui_info(int fd)
 {
    int ui_Fd;
    char  ui_info[1024];
    int len;
    char fd_uart[100];
    memset(fd_uart,0,sizeof(fd_uart));

    ui_Fd=fd;

    while(1)
    {
        len=read(ui_Fd,ui_info,MAX_UI_SIZE-1); //从串口获取gps信息
        if(len>0)
        {
            printf("\n>>> [RX] 串口收到数据, 长度: %d 字节\n", len);
            printf(">>> [RX] HEX报文: ");
            for(int i = 0; i < len; i++) {
                printf("%02X ", (uint8_t)ui_info[i]); // 强制转为无符号打印十六进制
            }
            printf("\n");
            /* 处理串口数据 */
            process_uart_info(ui_Fd,ui_info,len);
        }
    }

    close(ui_Fd);
    return;
}


/*
 * get_network_stats
 * - 功能: 直接解析 /proc/net/dev 获取指定网口（如 eth0）字节/包统计
 * - 参数: interface_name - 网口名，例如 "eth0"
 *           info_stat - 输出统计结构体指针
 * - 返回: 0 成功，-1 失败
 */
//统计eth0统计信息
int get_network_stats(const char *interface_name,Info_0x06_Statistics* info_stat) {
    FILE *fp;
    char buffer[1024];
    unsigned long rx_bytes, rx_packets, tx_bytes, tx_packets;
    fp = fopen("/proc/net/dev", "r");
    if (fp == NULL) {
        perror("fopen /proc/net/dev failed");
        return -1;
    }
    
    fgets(buffer, sizeof(buffer), fp); // 第一行标题
    fgets(buffer, sizeof(buffer), fp); // 第二行标题
    
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        char ifname[32];
        if (sscanf(buffer, " %[^:]:", ifname) == 1) {
            size_t len = strlen(ifname);
            while (len > 0 && ifname[len-1] == ' ') {
                ifname[--len] = '\0';
            }
            if (strcmp(ifname, interface_name) == 0) {
                int fields = sscanf(buffer, " %*[^:]: %lu %lu %*d %*d %*d %*d %*d %*d %lu %lu",
                        &rx_bytes, &rx_packets, &tx_bytes, &tx_packets);
                if(fields==4)
                {
                    info_stat->eth_rx_bytes=rx_bytes;
                    info_stat->eth_tx_bytes=tx_bytes;
                    info_stat->eth_rx_packets=rx_packets;
                    info_stat->eth_tx_packets=tx_packets;
                }
                break;
            }
        }
    }
    fclose(fp);
    return 0;
}


/*
 * get_interface_stats
 * - 功能: 使用 ifconfig 命令（popen）解析 eth0 的 RX/TX packets 字段
 * - 参数: info_stat - 输出的 Info_0x06_Statistics 指针（仅写入包计数）
 * - 返回: 0 成功，-1 失败
 * - 说明: 注意此解析依赖 ifconfig 的输出格式，嵌入式系统上格式可能略有差异。
 */
int get_interface_stats(Info_0x06_Statistics* info_stat) {
    FILE *fp;
    char buffer[1024];
    char command[128];
    unsigned long rx_packets = 0;
    unsigned long tx_packets = 0;
    uint8_t find_tx,find_rx;

    find_tx=find_rx=0;
    snprintf(command, sizeof(command), "ifconfig eth0");
    
    fp = popen(command, "r");
    if (fp == NULL) {
        perror("popen failed");
        return -1;
    }
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        if (strstr(buffer, "RX packets") != NULL) {
            if (sscanf(buffer, " RX packets : %lu", &rx_packets) == 1) {
                find_rx=1;
            } 
        }
        if (strstr(buffer, "TX packets") != NULL) {
            if (sscanf(buffer, " TX packets : %lu", &tx_packets) == 1) {
                find_tx=1;
            } 
        }       

        if(find_tx==1&&find_rx==1)
        {
            info_stat->eth_rx_packets=(uint16_t)rx_packets;
            info_stat->eth_tx_packets=(uint16_t)tx_packets;
            break;
        }
    }
    pclose(fp);
    return 0;
}


/*
 * write_ui_Thread
 * - 功能: 后台线程函数，周期性向 UI 发送各种报告帧（0x04/0x08/0x06/0x07/0x09）
 * - 参数: arg - 指向 int 的指针，包含已打开的 ui 串口 fd
 * - 行为要点:
 *     - 周期性读取 /etc/node_xwg 并发送 0x04（一次）与 0x08（周期）
 *     - 构造并伪造硬件/邻居数据用于 0x07/0x09 上报（在测试/无硬件时使用）
 */
void write_ui_Thread(void* arg)
{
    // 强制转换指针拿回 fd 
    int ui_Fd = *(int*)arg;
    printf("create thread: report uart info uart fd :%d \r\n", ui_Fd);

    struct mgmt_send self_msg;
    memset(&self_msg, 0, sizeof(self_msg));

    Node_Xwg_Pairs param_pairs[] = {
        {"channel", 0, 0, 0},{"power", 0, 0, 0},{"bw", 0, 0, 0},{"mcs", 0, 0, 0},
        {"macmode", 0, 0, 0},{"slotlen", 0, 0, 0},{"router", 0, 0, 0},{"workmode", 0, 0, 0},
        {"select_freq1", 0, 0, 0},{"select_freq2", 0, 0, 0},{"select_freq3", 0, 0, 0},{"select_freq4", 0, 0, 0},
        {"sync_mode", 0, 0, 0},{"kylb", 0, 0, 0}
    };
    int pair_count = sizeof(param_pairs) / sizeof(param_pairs[0]);

    // 0x04: 初始化发送一次配置参数
    read_node_xwg_file("/etc/node_xwg", param_pairs, pair_count);
    Send_0x04(ui_Fd, (void*)&param_pairs, sizeof(param_pairs));

    // ==========================================
    // 预先伪造一些全局固定数据 (针对 0x05, 0x06)
    // ==========================================
    
    // 伪造 0x06 网卡收发包统计基础值
    memset(&stat_info, 0, sizeof(Info_0x06_Statistics));
    stat_info.eth_tx_packets = 1000;
    stat_info.eth_rx_packets = 2500;
    stat_info.audio_tx_packets = 100;
    stat_info.audio_rx_packets = 150;

    // 伪造本机 ID 和 GPS 信息 (供给 0x05 协议使用)
    SELFID = 1;                     // 假装本机是 1 号节点
    gps_info_uart.lon = 116.407396; // 经度 (北京)
    gps_info_uart.lat = 39.904200;  // 纬度 (北京)
    gps_info_uart.gaodu = 50;       // 高度 50m
    gps_info_uart.bj_time[0] = 12;  // 12点
    gps_info_uart.bj_time[1] = 30;  // 30分
    gps_info_uart.bj_time[2] = 0;   // 0秒

    while(1)
    {
        // 在 while(1) 循环开始处添加
        FILE *test_fp = fopen("/etc/node_xwg", "r");
        if (test_fp == NULL) {
            printf("[ERROR] 线程内无法打开 /etc/node_xwg: %s\n", strerror(errno));
        } else {
            //printf("[SUCCESS] 线程成功找到配置文件\n");
            fclose(test_fp);
        }   

        // 0x08: 循环发送节点参数
        read_node_xwg_file("/etc/node_xwg", param_pairs, pair_count);
        /*
        printf("\n--- [自检] 当前 xwg 解析结果 ---\n");
        printf("信道 (channel) : %d\n", get_int_value(param_pairs, "channel"));
        printf("功率 (power)   : %d\n", get_int_value(param_pairs, "power"));
        printf("工作模式 (workmode) : %d\n", get_int_value(param_pairs, "workmode"));
        printf("--------------------------------\n");
        */
        Send_0x08(ui_Fd, (void*)&param_pairs, sizeof(param_pairs));

        // ==========================================
        // 伪造硬件状态与网络拓扑数据 (针对 0x07, 0x09)
        // ==========================================
        
        // 1. 0x07 设备状态数据固定值 (amp_infomation)
        self_msg.amp_infomation.battery_level = 88;          // 电池电量 88%
        self_msg.amp_infomation.temperature = 45;            // 温度 45度
        self_msg.amp_infomation.fan_status = 1;              // 风扇状态：1(运转)
        self_msg.amp_infomation.nav_lock_status = 1;         // 卫导锁定状态：已锁定
        self_msg.amp_infomation.power_ac220_power = 20;      // 功耗 20W
        self_msg.amp_infomation.rf_tx_rx_status = 1;         // 收发状态：发射
        self_msg.amp_infomation.rf_ch1_freq = 1400;          // 射频频点 1400MHz
        self_msg.amp_infomation.rf_ch1_rf_power = 30;        // 射频功率 30dBm

        // 2. 0x09 网络邻居拓扑数据固定值
        self_msg.neigh_num = 2;                              // 假装有 2 个邻居
        
        // 邻居 A
        self_msg.msg[0].node_id = 2;                         // 邻居ID 为 2
        self_msg.msg[0].rssi = 65;                           // 信号强度 (你的代码是 0-rssi, 传入65即-65dBm)
        self_msg.msg[0].time_jitter = 15;                    // 延迟 15ms
        
        // 邻居 B
        self_msg.msg[1].node_id = 5;                         // 邻居ID 为 5
        self_msg.msg[1].rssi = 80;                           // 信号强度 -80dBm
        self_msg.msg[1].time_jitter = 40;                    // 延迟 40ms

        // 3. 让网卡统计包和时间稍微动一下，前端页面看着像活的 (可选)
        stat_info.eth_tx_packets += 5;
        stat_info.eth_rx_packets += 8;
        gps_info_uart.bj_time[2] += 1;
        if(gps_info_uart.bj_time[2] >= 60) gps_info_uart.bj_time[2] = 0;


        // ==========================================
        // 发送所有数据包
        // ==========================================
        //Send_0x05(ui_Fd, &g_radio_param); 
        Send_0x06(ui_Fd, (void*)&stat_info); 
        Send_0x07(ui_Fd, &self_msg.amp_infomation);    
        Send_0x09(ui_Fd, &self_msg); 

        sleep(1);
    }
}

/*
 * get_ui_Thread
 * - 功能: 后台线程函数，负责循环调用 get_ui_info 从串口读取并处理 incoming 数据
 * - 参数: arg - 指向 int 的指针，包含已打开的 ui 串口 fd
 */
void get_ui_Thread(void* arg) 
{
    int fd=*(int*)arg; 
    printf("create thread: read uart info uart fd %d \r\n",fd);
    while (1) 
    {
        get_ui_info(fd);
        usleep(50000);
    }
}

