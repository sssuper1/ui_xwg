#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "sqlite_unit.h"
#include "mgmt_transmit.h"
#include "mgmt_types.h"
#include "socketUDP.h"
#include "SocketTCP.h"
#include <sys/stat.h>
#include <time.h>


uint8_t SELFID;
uint32_t FREQ_INIT;
uint16_t POWER_INIT;
uint8_t BW_INIT;
uint8_t MCS_INIT;
uint16_t MACMODE_INIT;
double longitude;
double latitude;
uint8_t NET_WORKMOD_INIT;
uint8_t RX_CHANNEL_MODE_INIT;
uint8_t SELFIP_s[4] = {192, 168, 2, 32};
uint8_t DEVICETYPE_INIT;
uint32_t HOP_FREQ_TB_INIT[32];
Node_Xwg_Pairs param_pairs[] = {
        {"channel", 0, 0},{"power", 0, 0},{"bw", 0, 0},{"mcs", 0, 0},
        {"macmode", 0, 0},{"slotlen", 0, 0},{"router", 0, 0},{"workmode", 0, 0},
		{"select_freq1", 0, 0},{"select_freq2", 0, 0},{"select_freq3", 0, 0},{"select_freq4", 0, 0},
		{"sync_mode",0,0},{"kylb",0,0}
    };

uint8_t g_u8SLOTLEN;
uint8_t POWER_LEVEL_INIT;
uint8_t POWER_ATTENUATION_INIT;
char version[20];
Smgmt_phy PHY_MSG;
volatile int g_config_dirty = 1;
// ==========================================================
// 1. 系统大管家初始化
// ==========================================================
void mgmt_system_init(void) {
    FILE* file_node;
    char nodemessage[100];
    char messagename[100];
    int param[9];
    uint8_t cmd[200];
    printf("[System Init] 开始读取配置文件 /etc/node_xwg...\n");

    // 赋默认值，防止文件读取失败
    FREQ_INIT = 1400;
    BW_INIT = 10;
    POWER_INIT = 30;

    if ((file_node = fopen("/etc/node_xwg", "r")) != NULL) {
        while (fgets(nodemessage, sizeof(nodemessage), file_node) != NULL) {
            bzero(messagename, sizeof(messagename));
            sscanf(nodemessage, "%s ", messagename);
            if (strcmp(messagename, "channel") == 0) {
                sscanf(nodemessage, "channel %d", &FREQ_INIT);
                printf("set ------- channel = %d\n", FREQ_INIT);
            }
            if (strcmp(messagename, "networkmode") == 0) {
                sscanf(nodemessage, "networkmode %d", &param[0]);
                NET_WORKMOD_INIT = param[0];
                printf("set ------- networkmode = %d\n", NET_WORKMOD_INIT);
            }
            if (strcmp(messagename, "devicetype") == 0) {
                sscanf(nodemessage, "devicetype %d", &param[0]);
                DEVICETYPE_INIT = param[0];
                printf("set ------- devicetype = %d\n", DEVICETYPE_INIT);
            }
            if (strcmp(messagename, "power") == 0) {
                sscanf(nodemessage, "power %d", &param[0]);
                POWER_INIT = param[0];
                printf("set ------- power = %d\n", POWER_INIT);
            }
            if (strcmp(messagename, "power_level") == 0) {
                sscanf(nodemessage, "power_level %d", &param[0]);
                POWER_LEVEL_INIT = param[0];
                printf("set ------- power_level = %d\n", POWER_LEVEL_INIT);
            }
            if (strcmp(messagename, "power_attenuation") == 0) {
                sscanf(nodemessage, "power_attenuation %d", &param[0]);
                POWER_ATTENUATION_INIT = param[0];
                printf("set ------- power_attenuation = %d\n", POWER_ATTENUATION_INIT);
            }
            if (strcmp(messagename, "rx_channel_mode") == 0) {
                sscanf(nodemessage, "rx_channel_mode %d", &param[0]);
                if (param[0] < 0) param[0] = 0;
                else if (param[0] > 4) param[0] = 4;
                RX_CHANNEL_MODE_INIT = param[0];
                printf("set ------- rx_channel_mode = %d\n", RX_CHANNEL_MODE_INIT);
            }
            if (strcmp(messagename, "bw") == 0) {
                sscanf(nodemessage, "bw %d", &param[0]);
                BW_INIT = param[0];
                printf("set ------- bw = %d\n", BW_INIT);
            }
            if (strcmp(messagename, "version") == 0) {
                sscanf(nodemessage, "version %s", version);
                printf("set ------- version = %s\n", version);
            }
            if (strcmp(messagename, "mcs") == 0) {
                sscanf(nodemessage, "mcs %d", &param[0]);
                MCS_INIT = param[0];
                printf("set ------- mcs = %d\n", MCS_INIT);
            }
            if (strcmp(messagename, "macmode") == 0) {
                sscanf(nodemessage, "macmode %d", &param[0]);
                MACMODE_INIT = param[0];
                printf("set ------- macmode = %d\n", MACMODE_INIT);
            }
            if (strcmp(messagename, "phymsg") == 0) {
                sscanf(nodemessage, "phymsg %d %d %d %d %d %d", &param[0],
                    &param[1], &param[2], &param[3], &param[4], &param[5]);
                PHY_MSG.rf_agc_framelock_en = param[0];
                PHY_MSG.phy_cfo_bypass_en = param[1];
                PHY_MSG.phy_pre_STS_thresh = param[2];
                PHY_MSG.phy_pre_LTS_thresh = param[3];
                PHY_MSG.phy_tx_iq0_scale = param[4];
                PHY_MSG.phy_tx_iq1_scale = param[5];
                printf("set ------- phymsg = %d %d %d %d %d %d\n", PHY_MSG.rf_agc_framelock_en, PHY_MSG.phy_cfo_bypass_en,
                      PHY_MSG.phy_pre_STS_thresh, PHY_MSG.phy_pre_LTS_thresh,
                      PHY_MSG.phy_tx_iq0_scale, PHY_MSG.phy_tx_iq1_scale);
            }

            if (strcmp(messagename, "ip_addr") == 0) {
                sscanf(nodemessage, "ip_addr %d.%d.%d.%d", &SELFIP_s[0],&SELFIP_s[1],&SELFIP_s[2],&SELFIP_s[3]);
                memset(cmd,0,sizeof(cmd));
                // 如果你的 imx6ull 用的网卡不叫 br0（比如叫 eth0），请将下面这里的 br0 改为对应的网卡名
                sprintf((char*)cmd, "ifconfig eth0 %d.%d.%d.%d", SELFIP_s[0],SELFIP_s[1],SELFIP_s[2],SELFIP_s[3]);
                //system((char*)cmd);
                printf("set ------- IP address = %d.%d.%d.%d\n", SELFIP_s[0],SELFIP_s[1],SELFIP_s[2],SELFIP_s[3]);
            }
            if (strcmp(messagename, "slotlen") == 0) {
                sscanf(nodemessage, "slotlen %d", (int*)&g_u8SLOTLEN);
                printf("set ------- slotlen = %d\n", g_u8SLOTLEN);  
            }
        }
        fclose(file_node);
    } else {
        printf("[System Init] 未找到 /etc/node_xwg,使用默认参数。\n");
    }

    // 假设本节点 ID 为 1
    SELFID = 1; 
    printf("[System Init] 初始化完成！\n");
}
void monitor_xwg_config(void) {
    // 关键点：使用 static，让它能“记住”上一次的修改时间
    static time_t last_mtime = 0; 
    struct stat file_stat;

    // 尝试获取文件当前状态
    if (stat("/etc/node_xwg", &file_stat) == 0) {
        
        // 1. 如果是刚开机第一次运行，先记录一下初始时间就退出
        if (last_mtime == 0) {
            last_mtime = file_stat.st_mtime;
            return; 
        }

        // 2. 如果发现当前的修改时间，和记忆中的时间对不上！
        if (file_stat.st_mtime != last_mtime) {
            printf("[独立监控] 侦测到 /etc/node_xwg 被 WinSCP 修改！\n");
            
            // 更新记忆时间，防止无限重复触发
            last_mtime = file_stat.st_mtime;

            read_node_xwg_file("/etc/node_xwg", param_pairs, MAX_XWG_PAIRS); // 重新读取文件，更新全局变量
            for (int i = 0; i < MAX_XWG_PAIRS; i++) {
                if (param_pairs[i].found == 1) {
                    if (strcmp(param_pairs[i].key, "channel") == 0) {
                        FREQ_INIT = atoi(param_pairs[i].value); // 字符串转整数
                    } 
                    else if (strcmp(param_pairs[i].key, "bw") == 0) {
                        BW_INIT = atoi(param_pairs[i].value);
                    }
                    else if (strcmp(param_pairs[i].key, "power") == 0) {
                        POWER_INIT = atoi(param_pairs[i].value);
                    }
                    else if (strcmp(param_pairs[i].key, "rate") == 0) {
                        MCS_INIT = atoi(param_pairs[i].value);
                    }
                    else if (strcmp(param_pairs[i].key, "device_type") == 0) {
                        DEVICETYPE_INIT = atoi(param_pairs[i].value);
                    }
                }
            }
            printf("[独立监控] 内存变量更新完毕！准备触发数据库同步...\n");
            // 举起标志位，告诉其他模块数据变了，赶紧去刷库！
            g_config_dirty = 1; 
        }
    }
}
void mgmt_get_msg(void) {
    printf("[Data Engine] 启动动态数据刷新线程...\n");
    stInData stdata;
    unsigned int loop_count = 0;
    srand(time(NULL));

    int bw_encoded = 1; 
    if (BW_INIT == 20 || BW_INIT == 0) bw_encoded = 0;
    else if (BW_INIT == 10 || BW_INIT == 1) bw_encoded = 1;
    else if (BW_INIT == 5 || BW_INIT == 2) bw_encoded = 2;
    else if (BW_INIT == 3) bw_encoded = 3;

    int power_encoded;
    if (DEVICETYPE_INIT == 3) power_encoded = POWER_INIT;
    else power_encoded = 39 - POWER_INIT;

    // 【关键修复：给宽带配置页也补发“基石入场券”！】
    memset((char*)&stdata, 0, sizeof(stdata));
    sprintf(stdata.name, "ipaddr"); sprintf(stdata.value, "%d.%d.%d.%d", SELFIP_s[0],SELFIP_s[1],SELFIP_s[2],SELFIP_s[3]); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_meshinfo(stdata);
    sprintf(stdata.name, "device"); sprintf(stdata.value, "%d", SELFID); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_meshinfo(stdata);
    sprintf(stdata.name, "g_ver"); sprintf(stdata.value, "%s", version); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_meshinfo(stdata);
    
    // 写入正常的配置参数
    sprintf(stdata.name, "rf_freq"); sprintf(stdata.value, "%d", FREQ_INIT); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_meshinfo(stdata);
    sprintf(stdata.name, "m_chanbw"); sprintf(stdata.value, "%d", bw_encoded); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_meshinfo(stdata);
    sprintf(stdata.name, "m_txpower"); sprintf(stdata.value, "%d", power_encoded); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_meshinfo(stdata);
    sprintf(stdata.name, "m_rate"); sprintf(stdata.value, "%d", MCS_INIT); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_meshinfo(stdata);
    sprintf(stdata.name, "devicetype"); sprintf(stdata.value, "%d", DEVICETYPE_INIT); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_meshinfo(stdata);

    // 【开机立刻强行同步一次，让网页一打开就能读到初始值！】
    system("cp /www/cgi-bin/test.db /www/cgi/test.db");



    while (1) {

        monitor_xwg_config();
        loop_count++;
        
        // --- 补充前端必备的基石字段 ---
        memset((char*)&stdata, 0, sizeof(stdata));
        sprintf(stdata.name, "ipaddr"); sprintf(stdata.value, "%d.%d.%d.%d", SELFIP_s[0],SELFIP_s[1],SELFIP_s[2],SELFIP_s[3]); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_systeminfo(stdata);
        sprintf(stdata.name, "device"); sprintf(stdata.value, "%d", SELFID); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_systeminfo(stdata);
        sprintf(stdata.name, "g_ver"); sprintf(stdata.value, "%s", version); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_systeminfo(stdata);
        
        // --- 1. 刷新顶部状态栏 (必须在循环内动态计算) ---
        int current_bw = 1; 
        if (BW_INIT == 20 || BW_INIT == 0) current_bw = 0;
        else if (BW_INIT == 10 || BW_INIT == 1) current_bw = 1;
        else if (BW_INIT == 5 || BW_INIT == 2) current_bw = 2;
        else if (BW_INIT == 3) current_bw = 3; 

        sprintf(stdata.name, "rf_freq"); sprintf(stdata.value, "%d", FREQ_INIT); stdata.state[0] = '0'; stdata.lib[0] = '0'; updateData_systeminfo(stdata);
        sprintf(stdata.name, "m_chanbw"); sprintf(stdata.value, "%d", current_bw); stdata.state[0] = '0'; stdata.lib[0] = '0'; updateData_systeminfo(stdata);
        sprintf(stdata.name, "m_txpower"); sprintf(stdata.value, "%d", POWER_INIT); stdata.state[0] = '0'; stdata.lib[0] = '0'; updateData_systeminfo(stdata);
        sprintf(stdata.name, "m_rate"); sprintf(stdata.value, "%d", MCS_INIT); stdata.state[0] = '0'; stdata.lib[0] = '0'; updateData_systeminfo(stdata);
        sprintf(stdata.name, "devicetype"); sprintf(stdata.value, "%d", DEVICETYPE_INIT); stdata.state[0] = '0'; stdata.lib[0] = '0'; updateData_systeminfo(stdata);

        // --- 2. 【核心修复】只有配置被修改时，才重新计算并刷新下拉框！ ---
        if (g_config_dirty == 1) {
            int current_power;
            if (DEVICETYPE_INIT == 3) current_power = POWER_INIT; 
            else current_power = 39 - POWER_INIT;

            sprintf(stdata.name, "rf_freq"); sprintf(stdata.value, "%d", FREQ_INIT); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_meshinfo(stdata);
            sprintf(stdata.name, "m_chanbw"); sprintf(stdata.value, "%d", current_bw); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_meshinfo(stdata);
            sprintf(stdata.name, "m_txpower"); sprintf(stdata.value, "%d", current_power); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_meshinfo(stdata);
            sprintf(stdata.name, "m_rate"); sprintf(stdata.value, "%d", MCS_INIT); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_meshinfo(stdata);
            sprintf(stdata.name, "devicetype"); sprintf(stdata.value, "%d", DEVICETYPE_INIT); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_meshinfo(stdata);
                        
            g_config_dirty = 0; // 清除脏标志，等待下一次用户修改
            printf("[Data Engine] 侦测到配置改变，已安全同步至数据库下拉框！\n");
        }

        // --- 3. 动态心跳 (邻居拓扑) ---
        int mock_snr = 20 + (rand() % 10);
        int mock_rssi = -80 + (rand() % 15);
        sprintf(stdata.name, "id1"); sprintf(stdata.value, "2"); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_systeminfo(stdata);
        sprintf(stdata.name, "ip1"); sprintf(stdata.value, "192.168.2.2"); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_systeminfo(stdata);
        sprintf(stdata.name, "snr1"); sprintf(stdata.value, "%d", mock_snr); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_systeminfo(stdata);
        sprintf(stdata.name, "rssi1"); sprintf(stdata.value, "%d", mock_rssi); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_systeminfo(stdata);
        sprintf(stdata.name, "run_time"); sprintf(stdata.value, "%d", loop_count * 5); stdata.state[0] = '1'; stdata.lib[0] = '0'; updateData_systeminfo(stdata);

        // 强力同步，把 cgi-bin 里最新的心跳刷给网页
        system("cp /www/cgi-bin/test.db /www/cgi/test.db");
        g_config_dirty = 0;
        sleep(5);
    }
}