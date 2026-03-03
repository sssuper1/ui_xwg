#include "sqlite_unit.h"
#include "mgmt_types.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "sqlite3.h"
#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include "Lock.h"
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "Lock.h"
#include "socketUDP.h"  // 必须包含UDP发送的头文件
#include "mgmt_netlink.h"
#include "mgmt_transmit.h"
#include "ui_get.h"

sqlite3 *g_psqlitedb;
stSurveyInfo g_stSurveyInfo;
bcMeshInfo meshinfo;
pthread_mutex_t sqlite3_mutex1;

int SOCKET_BCAST_SEND; 
uint8_t  rate_auto=0;   //0:fix  1:auto
struct sockaddr_in S_GROUND_BCAST;

extern uint32_t FREQ_INIT;
uint8_t SELFIP_s[4]; 

#define BCAST_SEND_PORT  7901   //组播端口
//transmit文件中的参数
uint8_t BW_INIT;
uint16_t POWER_INIT;
uint8_t NET_WORKMOD_INIT;
uint8_t DEVICETYPE_INIT;
static pthread_cond_t TCPCLIENTCOND_WG;
static pthread_mutex_t TCPCLIENTMUTEX_WG;
uint8_t POWER_LEVEL_INIT;
uint8_t RX_CHANNEL_MODE_INIT;
uint8_t POWER_ATTENUATION_INIT;
char version[20];




#define TEST_DB_SRC "/www/cgi-bin/test.db"
#define TEST_DB_DST "/www/cgi/test.db"
#define TEST_DB_TMP "/www/cgi/test.db.tmp"
#define TEST_DB_BAK "/www/cgi/test.db.bak"


/*
 * sqliteinit
 * - 功能: 打开 /www/cgi-bin/test.db（SQLite）并初始化内部数据
 * - 返回: 0 成功, 非 0 表示失败并关闭 db
 * - 说明:
 *     - 创建互斥锁 `sqlite3_mutex1` 用于多线程访问 sqlite
 *     - 成功打开数据库后会调用 `updateData_init` 将配置刷入表中
 */
int sqliteinit(void)
{
    sqlite3_mutex1 = CreateLock();//加锁

    int rc = sqlite3_open("/www/cgi-bin/test.db", &g_psqlitedb);//建库
    if (rc) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(g_psqlitedb));
        sqlite3_close(g_psqlitedb);
        return 1;
    }

    updateData_init(); 
    
    return 0; // 补充返回值
}

/*
 * busyHandle
 * - 功能: sqlite busy handler 回调，当数据库被占用时由 sqlite 调用
 * - 参数: ptr - user data（未使用），retry_times - 当前重试次数
 * - 返回: 非 0 表示继续重试（sleep 后重试）
 * - 说明: 本项目采用简单睡眠重试策略以避免立即失败。
 */
int busyHandle(void* ptr,int retry_times)
{
    printf("retry_times %d\n",retry_times);
    sqlite3_sleep(10);
    return 1;
}

/*
 * updateData_systeminfo_qk
 * - 功能: 快速更新 systemInfo 表中某个字段的 value 与 state
 * - 参数: name  - systemInfo 表中的 name 字段（例如 "rf_freq"）
 *           value - 要写入的整数值
 * - 说明:
 *     - 使用 sqlite3_busy_handler 设置 busy 回调
 *     - 使用 `Lock`/`Unlock` 对 sqlite 访问加锁，保证线程安全
 */

void updateData_systeminfo_qk(const char* name,const int value)
{
    char value_str[32];
    snprintf(value_str, sizeof(value_str), "%d", value);

    char updateSql[SQLDATALEN];
    int rc ;

    snprintf(updateSql, sizeof(updateSql), "UPDATE systemInfo SET value = '%s', state = '1', lib = '0' WHERE name = '%s';" \
            , value_str,name);

    char* errMsg;
    sqlite3_busy_handler(g_psqlitedb,busyHandle,NULL);
    Lock(&sqlite3_mutex1,0);
   
    rc = sqlite3_exec(g_psqlitedb, updateSql, NULL, 0, &errMsg);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "无法更新数据: %s\n", errMsg);
    }

    Unlock(&sqlite3_mutex1);    
}

/*
 * updateData_meshinfo_qk
 * - 功能: 快速更新 meshInfo 表中某个字段的 value 与 state
 * - 参数与说明同 updateData_systeminfo_qk，但针对 meshInfo 表
 */
void updateData_meshinfo_qk(const char* name,const int value)
{
    char value_str[32];
    snprintf(value_str, sizeof(value_str), "%d", value);

    char updateSql[SQLDATALEN];
    int rc ;

    snprintf(updateSql, sizeof(updateSql), "UPDATE meshInfo SET value = '%s', state = '1', lib = '0' WHERE name = '%s';" \
            , value_str,name);

    char* errMsg;
    sqlite3_busy_handler(g_psqlitedb,busyHandle,NULL);
    Lock(&sqlite3_mutex1,0);
   
    rc = sqlite3_exec(g_psqlitedb, updateSql, NULL, 0, &errMsg);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "无法更新数据: %s\n", errMsg);
    }

    Unlock(&sqlite3_mutex1);
}

/*
 * copy_file_contents
 * - 功能: 将 src 文件内容可靠复制到 dst（使用 read/write 并 fsync）
 * - 返回: 0 成功，-1 失败
 * - 说明: 用于在持久化 test.db 时先写入临时文件再替换，保证原子性/数据落盘
 */

static int copy_file_contents(const char *src, const char *dst)
{
	int in_fd = open(src, O_RDONLY);
	if (in_fd < 0)
		return -1;

	int out_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (out_fd < 0) {
		close(in_fd);
		return -1;
	}

	char buffer[4096];
	ssize_t rd;
	while ((rd = read(in_fd, buffer, sizeof(buffer))) > 0) {
		char *ptr = buffer;
		ssize_t remaining = rd;
		while (remaining > 0) {
			ssize_t wr = write(out_fd, ptr, remaining);
			if (wr <= 0) {
				close(in_fd);
				close(out_fd);
				return -1;
			}
			ptr += wr;
			remaining -= wr;
		}
	}

	if (rd < 0) {
		close(in_fd);
		close(out_fd);
		return -1;
	}

	if (fsync(out_fd) != 0) {
		close(in_fd);
		close(out_fd);
		return -1;
	}

	close(in_fd);
	close(out_fd);

	return 0;
}

/*
 * sqlite_quick_check_cb
 * - 功能: 用作 PRAGMA quick_check; 的回调，检测数据库完整性是否返回 "ok"
 * - 参数: data - bool 指针，argc/argv 为 sqlite 回调参数
 */
static int sqlite_quick_check_cb(void *data, int argc, char **argv, char **azColName)
{
	bool *ok = (bool *)data;
	if (argc > 0 && argv[0] && strcmp(argv[0], "ok") == 0) {
		*ok = true;
	}
	return 0;
}

/*
 * sqlite_db_is_valid
 * - 功能: 通过 `PRAGMA quick_check;` 检查当前打开的 g_psqlitedb 的完整性
 * - 返回: true 表示数据库完整，false 表示损坏或无法执行检查
 */
static bool sqlite_db_is_valid(void)
{
	bool ok = false;
	char *errmsg = NULL;

	if (!g_psqlitedb)
		return false;

	if (SQLITE_OK != sqlite3_exec(g_psqlitedb, "PRAGMA quick_check;", sqlite_quick_check_cb, &ok, &errmsg)) {
		if (errmsg) {
			sqlite3_free(errmsg);
		}
		return false;
	}

	return ok;
}

/*
 * persist_test_db
 * - 功能: 将运行时的 /www/cgi-bin/test.db 持久化到 /www/cgi/test.db
 * - 返回: true 成功，false 失败
 * - 操作步骤:
 *     1. 检查源文件是否存在且非空
 *     2. 通过 sqlite_db_is_valid 验证数据库完整性
 *     3. 将源复制为临时文件 TEST_DB_TMP
 *     4. 轮换备份（rename DST -> BAK），将 TMP -> DST
 *     5. sync 确保写盘
 * - 说明: 该函数在数据库参数更改后被调用，用于把内存数据库持久保存到 web 可读目录
 */

bool persist_test_db(void)
{
    struct stat st;

    if (stat(TEST_DB_SRC, &st) != 0 || st.st_size <= 0) {
        printf("[sqlite_unit] skip persist test.db: empty or missing source\n");
        return false;
    }

    if (!sqlite_db_is_valid()) {
        printf("[sqlite_unit] skip persist test.db: integrity check failed\n");
        return false;
    }

    if (copy_file_contents(TEST_DB_SRC, TEST_DB_TMP) != 0) {
        printf("[sqlite_unit] failed to create temporary test.db copy: %s\n", strerror(errno));
        unlink(TEST_DB_TMP);
        return false;
    }

    sync();

    if (rename(TEST_DB_DST, TEST_DB_BAK) != 0 && errno != ENOENT) {
        printf("[sqlite_unit] warning: could not rotate previous test.db: %s\n", strerror(errno));
    }

    if (rename(TEST_DB_TMP, TEST_DB_DST) != 0) {
        printf("[sqlite_unit] failed to promote new test.db: %s\n", strerror(errno));
        unlink(TEST_DB_TMP);
        if (rename(TEST_DB_BAK, TEST_DB_DST) != 0 && errno != ENOENT) {
            printf("[sqlite_unit] warning: could not restore previous test.db from backup\n");
        }
        return false;
    }

    sync();
    return true;
}


/*
 * sqlite_set_userinfo_callback
 * - 功能: sqlite 查询回调，用于处理 userInfo 表的行，当 state 字段是 '1' 时应用设置并把 state 置 '0'
 * - 参数: 与 sqlite3_exec 回调一致：NotUsed, argc, argv, azColName
 * - 关键行为:
 *     - 当 name 为 "m_ip" 且 state=="1" 时，解析 ip 并调用 ifconfig 更新 br0
 *     - 更新数据库中对应行的 state 为 '0'（通过 UPDATE userInfo）
 *     - 当改变了 IP 等关键配置后，调用 persist_test_db 将 test.db 持久化
 * - 说明: 该回调在 sqlite_exec 查询 userInfo 时被触发，可能会多次被调用（每行一次）
 */

static int sqlite_set_userinfo_callback(void *NotUsed, int argc, char **argv, char **azColName) {
    char *zErrMsg = 0;
    char updateSql[100];
    int counttmp = 0;
    uint8_t cmd[200];
    bool isset = false; // TRUE/FALSE 换成小写 true/false

    if(0 == strcmp(argv[2],"1"))
    {
        if(0 == strcmp(argv[0],"m_ip"))
        {
            sscanf(argv[1], "%d.%d.%d.%d", &SELFIP_s[0],&SELFIP_s[1],&SELFIP_s[2],&SELFIP_s[3]);
            memset(cmd,0,sizeof(cmd));
            sprintf((char*)cmd, "ifconfig br0 %d.%d.%d.%d", SELFIP_s[0],SELFIP_s[1],SELFIP_s[2],SELFIP_s[3]);
            system((char*)cmd);
                
            printf("set ------- br0 ip address = %d.%d.%d.%d\n", SELFIP_s[0],SELFIP_s[1],SELFIP_s[2],SELFIP_s[3]);
            memset(cmd,0,sizeof(cmd));
            sprintf((char*)cmd, "sed -i \"s/ip_addr .*/ip_addr %s/g\" /mnt/node_xwg", argv[1]);
            system((char*)cmd);
            system("sync");
            isset = true;   
        }

        snprintf(updateSql, sizeof(updateSql), "UPDATE userInfo SET state = '0' WHERE name = '%s';", argv[0]);
        
        sqlite3_busy_handler(g_psqlitedb,busyHandle,NULL);
        Lock(&sqlite3_mutex1,0);
        while (SQLITE_OK != sqlite3_exec(g_psqlitedb, updateSql, NULL, 0, &zErrMsg)) {
            counttmp ++;
            if(counttmp > 10) break;
            sqlite3_busy_handler(g_psqlitedb,busyHandle,NULL);
        }
        Unlock(&sqlite3_mutex1);
        
        if(isset)
        {
            isset = false;
            sleep(1);
            if (!persist_test_db()) {
                printf("[sqlite_unit] persist test.db failed after userinfo change\n");
            }
        }
    }
    return 0;
}

/*
 * sqlite_set_meshinfo_callback
 * - 功能: sqlite 查询回调，用于处理 meshInfo 表的行，当 state 字段为 '1' 时把设置应用到内核/系统
 * - 参数: 同 sqlite 回调规范
 * - 关键行为:
 *     - 解析多种 meshInfo 字段（如 m_txpower, rf_freq, m_chanbw, m_rate, workmode 等）
 *     - 将解析出的参数写入到 meshinfo 全局结构并标记 *_isset
 *     - 最终会用 mgmt_netlink_set_param 下发到内核（通过 sqlite_set_param 的循环）
 * - 说明: 此回调会对系统进行实际配置修改（例如调用脚本、修改 /etc/node_xwg）
 */
static int sqlite_set_meshinfo_callback(void *NotUsed, int argc, char **argv, char **azColName) {
    char *zErrMsg = 0;
    char updateSql[100];
    int countTmp = 0;
    int m_chanbw;
    int m_rate;
    int m_freq;
    int m_power;
    int workmode;

    INT8 buffer[sizeof(Smgmt_header) + sizeof(Smgmt_set_param)];
    INT32 buflen = sizeof(Smgmt_header) + sizeof(Smgmt_set_param);
    Smgmt_header* mhead = (Smgmt_header*)buffer;
    Smgmt_set_param* mparam = (Smgmt_set_param*)mhead->mgmt_data;
    bzero(buffer, buflen);
    mhead->mgmt_head = htons(HEAD);
    mhead->mgmt_len = sizeof(Smgmt_set_param);
    mhead->mgmt_type = 0;

    if(0 == strcmp(argv[4],"1"))
    {
        if(0 == strcmp(argv[0],"m_txpower"))
        {
            sscanf(argv[1], "%d", &m_power);
            mparam->mgmt_mac_txpower = htons((uint16_t)m_power);
            meshinfo.m_txpower=mparam->mgmt_mac_txpower;
            meshinfo.txpower_isset=1;
            meshinfo.sys_power=m_power;
        }
        if(0 == strcmp(argv[0],"rf_freq"))
        {
             sscanf(argv[1], "%d", &(mparam->mgmt_mac_freq));   
             mparam->mgmt_mac_freq = htonl(mparam->mgmt_mac_freq);
             meshinfo.rf_freq=mparam->mgmt_mac_freq;    
             meshinfo.freq_isset=1;
             sscanf(argv[1], "%d", &(m_freq));
             meshinfo.sys_freq=m_freq;
        }
        if(0 == strcmp(argv[0],"m_chanbw"))
        {
            sscanf(argv[1], "%d", &(mparam->mgmt_mac_bw));
            meshinfo.m_chanbw=mparam->mgmt_mac_bw;
            meshinfo.chanbw_isset=1;
            if(mparam->mgmt_mac_bw==0) m_chanbw=20;
            else if(mparam->mgmt_mac_bw==1) m_chanbw=10;
            else if(mparam->mgmt_mac_bw==2) m_chanbw=5;
            meshinfo.sys_bw=mparam->mgmt_mac_bw;
        }
        if(0 == strcmp(argv[0],"m_rate"))
        {
            sscanf(argv[1], "%d", &(mparam->mgmt_virt_unicast_mcs));
            meshinfo.m_rate=mparam->mgmt_virt_unicast_mcs;
            meshinfo.rate_isset=1;
            sscanf(argv[1], "%d", &(m_rate));
            meshinfo.sys_rate=m_rate;
            if(m_rate<0) {
                rate_auto=1;
                meshinfo.rate_isset=0;
            }
        }
        if(0 == strcmp(argv[0],"m_bcastmode"))
        {
            sscanf(argv[1], "%d", &(meshinfo.m_bcastmode));
        }
        if(0 == strcmp(argv[0],"workmode"))
        {
            sscanf(argv[1], "%d", &(mparam->mgmt_net_work_mode.NET_work_mode));
            meshinfo.workmode=mparam->mgmt_net_work_mode.NET_work_mode;
            meshinfo.workmode_isset=1;
            sscanf(argv[1], "%d", &(workmode));
            meshinfo.sys_workmode=workmode;
        }
        if(0 == strcmp(argv[0],"m_route"))
        {
            sscanf(argv[1], "%d", &(meshinfo.m_route));
            meshinfo.route_isset=1;
        }
        if(0 == strcmp(argv[0],"m_slot_len"))
        {
            sscanf(argv[1], "%d", &(meshinfo.m_slot_len));
            meshinfo.slot_isset=1;
        }
        if(0 == strcmp(argv[0],"m_trans_mode"))
        {
            sscanf(argv[1], "%d", &(meshinfo.m_trans_mode));
            meshinfo.m_trans_mode=htons(meshinfo.m_trans_mode);
            meshinfo.trans_mode_isset=1;
        }
        if(0 == strcmp(argv[0],"m_select_freq1"))
        {
            sscanf(argv[1], "%d", &(meshinfo.m_select_freq_1));
            meshinfo.select_freq_isset=1;
        }
        if(0 == strcmp(argv[0],"m_select_freq2"))
        {
            sscanf(argv[1], "%d", &(meshinfo.m_select_freq_2));
            meshinfo.select_freq_isset=1;
        }       
        if(0 == strcmp(argv[0],"m_select_freq3"))
        {
            sscanf(argv[1], "%d", &(meshinfo.m_select_freq_3));
            meshinfo.select_freq_isset=1;
        }
        if(0 == strcmp(argv[0],"m_select_freq4"))
        {
            sscanf(argv[1], "%d", &(meshinfo.m_select_freq_4));
            meshinfo.select_freq_isset=1;
        }
        if(0 == strcmp(argv[0],"power_level"))
        {
            int level = 0;
            sscanf(argv[1], "%d", &level);
            meshinfo.power_level = (uint8_t)level;
            meshinfo.sys_power_level = level;
            meshinfo.power_level_isset = 1;
        }
        if(0 == strcmp(argv[0],"power_attenuation"))
        {
            int attenuation = 0;
            sscanf(argv[1], "%d", &attenuation);
            meshinfo.power_attenuation = (uint8_t)attenuation;
            meshinfo.sys_power_attenuation = attenuation;
            meshinfo.power_attenuation_isset = 1;
        }
        if(0 == strcmp(argv[0],"rx_channel_mode"))
        {
            int rx_mode = 0;
            sscanf(argv[1], "%d", &rx_mode);
            if (rx_mode < 0) rx_mode = 0;
            else if (rx_mode > 4) rx_mode = 4;
            meshinfo.rx_channel_mode = (uint8_t)rx_mode;
            meshinfo.sys_rx_channel_mode = rx_mode;
            meshinfo.rx_channel_mode_isset = 1;
        }

        snprintf(updateSql, sizeof(updateSql), "UPDATE meshInfo SET state = '0' WHERE name = '%s';", argv[0]);
        sqlite3_busy_handler(g_psqlitedb,busyHandle,NULL);
        Lock(&sqlite3_mutex1,0);

        while (SQLITE_OK != sqlite3_exec(g_psqlitedb, updateSql, NULL, 0, &zErrMsg)) {
            fprintf(stderr, "SQL error1: %s\n", zErrMsg);
            sqlite3_free(zErrMsg);
            countTmp ++;
            if(countTmp > 5) break;
            sqlite3_busy_handler(g_psqlitedb,busyHandle,NULL);
        }
        Unlock(&sqlite3_mutex1);
    }
    return 0;
}

//将当前内存中的最新参数持久化到配置文件 /etc/node_xwg 中，以便重启后加载
void save_config_to_file(void) {
    FILE *fp = fopen("/etc/node_xwg", "w");
    if (fp) {
        fprintf(fp, "version V1.0\n");
        fprintf(fp, "devicetype %d\n", DEVICETYPE_INIT);
        fprintf(fp, "ip_addr %d.%d.%d.%d\n", SELFIP_s[0], SELFIP_s[1], SELFIP_s[2], SELFIP_s[3]);
        fprintf(fp, "networkmode %d\n", NET_WORKMOD_INIT);
        fprintf(fp, "macmode %d\n", MACMODE_INIT);
        fprintf(fp, "slotlen %d\n", g_u8SLOTLEN);
        
        // 核心射频参数
        fprintf(fp, "channel %d\n", FREQ_INIT);
        fprintf(fp, "power %d\n", POWER_INIT);
        fprintf(fp, "bw %d\n", BW_INIT);
        fprintf(fp, "mcs %d\n", MCS_INIT);
        
        // 功放与高级参数
        fprintf(fp, "power_level %d\n", POWER_LEVEL_INIT);
        fprintf(fp, "power_attenuation %d\n", POWER_ATTENUATION_INIT);
        fprintf(fp, "rx_channel_mode %d\n", RX_CHANNEL_MODE_INIT);
        fprintf(fp, "phymsg %d %d %d %d %d %d\n", 
                PHY_MSG.rf_agc_framelock_en, PHY_MSG.phy_cfo_bypass_en, 
                PHY_MSG.phy_pre_STS_thresh, PHY_MSG.phy_pre_LTS_thresh, 
                PHY_MSG.phy_tx_iq0_scale, PHY_MSG.phy_tx_iq1_scale);
        
        fclose(fp);
        system("sync"); // 强制刷入 Flash
        printf("[下发中心] 配置已成功保存到 /etc/node_xwg\n");
    } else {
        printf("[下发中心] 错误：无法打开 /etc/node_xwg 进行写入！\n");
    }
}
/*
 * sqlite_set_param
 * - 功能: 后台线程函数，轮询数据库（userInfo, meshInfo）并将标记为待下发的配置应用到系统
 * - 参数: arg - 未使用（线程接口要求）
 * - 主要流程:
 *     1. 使用 sqlite3_exec 查询 userInfo / meshInfo，分别由回调处理并标记 meshinfo 结构
 *     2. 根据 meshinfo 中的 *_isset 字段构造 mgmt_netlink 下发缓冲（或 UDP 组播）
 *     3. 对于某些设置同时修改 /etc/node_xwg 或执行脚本以使设置持久化和生效
 * - 说明: 该线程对 sqlite 使用 `sqlite3_busy_handler`，并在数据库访问时使用 Lock/Unlock 保证互斥
 */

void* sqlite_set_param(void* arg) {
    char *zErrMsg = 0;
    int opt=1;
    bool isset=false; // TRUE 改为 false
    int ret;
    static uint32_t s_fix_freq=0;   //   定频中心频率
    s_fix_freq=FREQ_INIT;

    double slot_info[]={0.5,1,1.25,2};     //时隙长度
    char bcast_buf[1000];
    char cmd[256];
    INT8 buffer[sizeof(Smgmt_header) + sizeof(Smgmt_set_param)];
    INT32 buflen = sizeof(Smgmt_header) + sizeof(Smgmt_set_param);
    Smgmt_header* mhead = (Smgmt_header*)buffer;
    Smgmt_set_param* mparam = (Smgmt_set_param*)mhead->mgmt_data;
    
    bzero(buffer, buflen);
    mhead->mgmt_head = htons(HEAD);
    mhead->mgmt_len = sizeof(Smgmt_set_param);
    mhead->mgmt_type = 0;

    memset((char*)&meshinfo,0,sizeof(meshinfo));
    meshinfo.m_bcastmode=1; //初始化非组播模式

    S_GROUND_BCAST.sin_family=AF_INET;
    S_GROUND_BCAST.sin_addr.s_addr = inet_addr("192.168.2.255"); //设置成广播
    S_GROUND_BCAST.sin_port = htons(BCAST_SEND_PORT);
    SOCKET_BCAST_SEND=socket (AF_INET,SOCK_DGRAM, 0 );
    if(SOCKET_BCAST_SEND <= 0)
    {
        printf("create bcast socket failed\r\n");
    }
    setsockopt(SOCKET_BCAST_SEND,SOL_SOCKET,SO_BROADCAST,&opt,sizeof(opt));

    while (1) {
        sqlite3_busy_handler(g_psqlitedb,busyHandle,NULL);
        sqlite3_exec(g_psqlitedb,"SELECT * FROM userInfo;",sqlite_set_userinfo_callback, 0, &zErrMsg);
        sqlite3_exec(g_psqlitedb,"SELECT * FROM meshInfo;",sqlite_set_meshinfo_callback, 0, &zErrMsg);

        if(meshinfo.m_bcastmode==1)
        {
            bzero(buffer, buflen);
            mhead->mgmt_head = htons(HEAD);
            mhead->mgmt_len = sizeof(Smgmt_set_param);
            mhead->mgmt_type = 0;
            
            if(meshinfo.txpower_isset) {
                meshinfo.txpower_isset=0; isset=true;
                mhead->mgmt_type |= MGMT_SET_POWER;
                mparam->mgmt_mac_txpower=meshinfo.m_txpower;
                memcpy(mparam->mgmt_mac_txpower_ch, meshinfo.m_txpower_ch, sizeof(meshinfo.m_txpower_ch));
            }
            if(meshinfo.power_level_isset) {
                meshinfo.power_level_isset=0; isset=true;
                mhead->mgmt_keep |= MGMT_SET_POWER_LEVEL;
                mparam->mgmt_mac_power_level=meshinfo.power_level;
            }
            if(meshinfo.power_attenuation_isset) {
                meshinfo.power_attenuation_isset=0; isset=true;
                mhead->mgmt_keep |= MGMT_SET_POWER_ATTENUATION;
                mparam->mgmt_mac_power_attenuation=meshinfo.power_attenuation;
            }
            if(meshinfo.rx_channel_mode_isset) {
                meshinfo.rx_channel_mode_isset=0; isset=true;
                mhead->mgmt_keep |= MGMT_SET_RX_CHANNEL_MODE;
                mparam->mgmt_rx_channel_mode = meshinfo.rx_channel_mode;
            }
            if(meshinfo.freq_isset) {
                meshinfo.freq_isset=0;
                if(meshinfo.workmode!=4) {
                    isset=true;
                    mhead->mgmt_type |= MGMT_SET_FREQUENCY;
                    mparam->mgmt_mac_freq=meshinfo.rf_freq;
                    s_fix_freq=mparam->mgmt_mac_freq;  
                }
            }
            if(meshinfo.chanbw_isset) {
                meshinfo.chanbw_isset=0; isset=true;            
                mhead->mgmt_type |= MGMT_SET_BANDWIDTH;
                mparam->mgmt_mac_bw=meshinfo.m_chanbw;
            }
            if(meshinfo.rate_isset) {
                meshinfo.rate_isset=0; isset=true;
                mhead->mgmt_type |= MGMT_SET_UNICAST_MCS;
                mparam->mgmt_virt_unicast_mcs=meshinfo.m_rate;
            }
            if(meshinfo.workmode_isset) {
                meshinfo.workmode_isset=0; isset=true;
                mhead->mgmt_type |= MGMT_SET_WORKMODE;
                mparam->mgmt_net_work_mode.NET_work_mode=meshinfo.workmode;

                if(meshinfo.workmode==1) {
                    mhead->mgmt_type |= MGMT_SET_FREQUENCY;                
                    mparam->mgmt_mac_freq=s_fix_freq;
                    sprintf(cmd, "sed -i \"s/workmode .*/workmode %d/g\" /etc/node_xwg", 1);    
                    system(cmd);                        
                } 
                if(meshinfo.workmode==4) {
                    if(meshinfo.select_freq_isset==1) {
                        meshinfo.select_freq_isset=0;
                        mparam->mgmt_net_work_mode.fh_len=4;
                        mparam->mgmt_net_work_mode.hop_freq_tb[0]=meshinfo.m_select_freq_1;
                        mparam->mgmt_net_work_mode.hop_freq_tb[1]=meshinfo.m_select_freq_2;
                        mparam->mgmt_net_work_mode.hop_freq_tb[2]=meshinfo.m_select_freq_3;
                        mparam->mgmt_net_work_mode.hop_freq_tb[3]=meshinfo.m_select_freq_4;
                    } else {
                        mparam->mgmt_net_work_mode.fh_len=4;
                        mparam->mgmt_net_work_mode.hop_freq_tb[0]=s_fix_freq;
                        mparam->mgmt_net_work_mode.hop_freq_tb[1]=s_fix_freq;
                        mparam->mgmt_net_work_mode.hop_freq_tb[2]=s_fix_freq;
                        mparam->mgmt_net_work_mode.hop_freq_tb[3]=s_fix_freq;
                    }
                    sprintf(cmd, "sed -i \"s/workmode .*/workmode %d/; s/select_freq1 .*/select_freq1 %d/; s/select_freq2 .*/select_freq2 %d/; s/select_freq3 .*/select_freq3 %d/; s/select_freq4 .*/select_freq4 %d/\" /etc/node_xwg", 
                        4, mparam->mgmt_net_work_mode.hop_freq_tb[0], mparam->mgmt_net_work_mode.hop_freq_tb[1], 
                        mparam->mgmt_net_work_mode.hop_freq_tb[2], mparam->mgmt_net_work_mode.hop_freq_tb[3]);
                    system(cmd);
                }
            }
            if(meshinfo.route_isset) {
                meshinfo.route_isset=0;
                switch(meshinfo.m_route) {
                    case 0: // KD_ROUTING_OLSR 假设值为0，以宏定义为准
                        ret = system("/home/root/cs_olsr.sh");
                        break;
                    case 1: // KD_ROUTING_AODV
                        ret = system("/home/root/cs_aodv.sh");
                        break;
                    case 2: // KD_ROUTING_CROSS_LAYER
                         ret = system("/home/root/cs_batman.sh");
                        break;
                }               
                sprintf(cmd, "sed -i \"s/router .*/router %d/g\" /etc/node_xwg", meshinfo.m_route);      
                system(cmd);
            }
            if(meshinfo.slot_isset) {   
                meshinfo.slot_isset=0; isset=true;
                mhead->mgmt_keep |= MGMT_SET_SLOTLEN;
                mparam->u8Slotlen=meshinfo.m_slot_len;
            }
            if(meshinfo.trans_mode_isset) {
                isset=true; meshinfo.trans_mode_isset=0;
                mhead->mgmt_type |= MGMT_SET_TEST_MODE;
                mparam->mgmt_mac_work_mode=meshinfo.m_trans_mode;
            }

            if(isset) {
                isset=false;
                mhead->mgmt_type = htons(mhead->mgmt_type);
                mhead->mgmt_keep = htons(mhead->mgmt_keep);
                mgmt_netlink_set_param((char*)buffer, buflen, NULL);    
                sleep(1);
                if (!persist_test_db()) {
                    printf("[sqlite_unit] persist test.db failed after meshinfo change\n");
                }
            }
        } else {
            // 组播模式
            bzero(buffer, buflen);
            mhead->mgmt_head = htons(HEAD);
            mhead->mgmt_len = sizeof(Smgmt_set_param);
            mhead->mgmt_type = 0;
            memcpy(bcast_buf,&meshinfo,sizeof(meshinfo));
            if(SendUDPClient(SOCKET_BCAST_SEND, bcast_buf, sizeof(meshinfo), &S_GROUND_BCAST) < 0) {
                printf("send broadcast packet fail\r\n");
            }
            sleep(4); 
            if(meshinfo.txpower_isset)
			{
				meshinfo.txpower_isset=0;
				isset=TRUE;
				mhead->mgmt_type |= MGMT_SET_POWER;
				//mhead->mgmt_type = htons(mhead->mgmt_type);
				mparam->mgmt_mac_txpower=meshinfo.m_txpower;
				memcpy(mparam->mgmt_mac_txpower_ch, meshinfo.m_txpower_ch, sizeof(meshinfo.m_txpower_ch));
				//printf("set txpower %d \r\n",mparam->mgmt_mac_txpower);
				//mgmt_netlink_set_param(buffer, buflen,NULL);
				
			
			}
			if(meshinfo.power_level_isset)
			{
				meshinfo.power_level_isset=0;
				isset=TRUE;
				mhead->mgmt_keep |= MGMT_SET_POWER_LEVEL;
				mparam->mgmt_mac_power_level=meshinfo.power_level;
			}
			if(meshinfo.power_attenuation_isset)
			{
				meshinfo.power_attenuation_isset=0;
				isset=TRUE;
				mhead->mgmt_keep |= MGMT_SET_POWER_ATTENUATION;
				mparam->mgmt_mac_power_attenuation=meshinfo.power_attenuation;
			}
			if(meshinfo.rx_channel_mode_isset)
			{
				meshinfo.rx_channel_mode_isset=0;
				isset=TRUE;
				mhead->mgmt_keep |= MGMT_SET_RX_CHANNEL_MODE;
				mparam->mgmt_rx_channel_mode = meshinfo.rx_channel_mode;
			}
			if(meshinfo.freq_isset)
			{
				meshinfo.freq_isset=0;
				isset=TRUE;
				mhead->mgmt_type |= MGMT_SET_FREQUENCY;
				//mhead->mgmt_type = htons(mhead->mgmt_type);
				mparam->mgmt_mac_freq=meshinfo.rf_freq;
				//printf("set freq %d \r\n",mparam->mgmt_mac_freq);
				//mgmt_netlink_set_param(buffer, buflen,NULL);
				// sprintf(cmd,
				// 	"cp /www/cgi-bin/test.db /www/cgi");
				// system(cmd);
				
			}
			if(meshinfo.chanbw_isset)
			{
				meshinfo.chanbw_isset=0;
				isset=TRUE;
				mhead->mgmt_type |= MGMT_SET_BANDWIDTH;
				//mhead->mgmt_type = htons(mhead->mgmt_type);
				mparam->mgmt_mac_bw=meshinfo.m_chanbw;
				//printf("set bw %d \r\n",mparam->mgmt_mac_bw);
				//mgmt_netlink_set_param(buffer, buflen,NULL);
				// sprintf(cmd,
				// 	"cp /www/cgi-bin/test.db /www/cgi");
				// system(cmd);
				
			}
			if(meshinfo.rate_isset)
			{
				meshinfo.rate_isset=0;
				mhead->mgmt_type |= MGMT_SET_UNICAST_MCS;
				isset=TRUE;
				//mhead->mgmt_type = htons(mhead->mgmt_type);
				mparam->mgmt_virt_unicast_mcs=meshinfo.m_rate;
				//printf("set rate %d \r\n",mparam->mgmt_virt_unicast_mcs);
				//mgmt_netlink_set_param(buffer, buflen,NULL);
				// sprintf(cmd,
				// 	"cp /www/cgi-bin/test.db /www/cgi");
				// system(cmd);
				
			}
			if(meshinfo.workmode_isset)
			{
				meshinfo.workmode_isset=0;
				isset=TRUE;
				mhead->mgmt_type |= MGMT_SET_WORKMODE;
				//mhead->mgmt_type = htons(mhead->mgmt_type);
				mparam->mgmt_net_work_mode.NET_work_mode=meshinfo.workmode;
				//printf("set rate %d\r\n",mparam->mgmt_virt_unicast_mcs);
				//mgmt_netlink_set_param(buffer, buflen,NULL);
				// sprintf(cmd,
				// 	"cp /www/cgi-bin/test.db /www/cgi");
				// system(cmd);

			}
			if(isset)
			{
				isset=FALSE;
				mhead->mgmt_type = htons(mhead->mgmt_type);
				mhead->mgmt_keep = htons(mhead->mgmt_keep);
				mgmt_netlink_set_param(buffer, buflen,NULL);
				if (!persist_test_db()) {
					printf("[sqlite_unit] persist test.db failed during meshinfo multicast sync\n");
				}
			}

        }   
        sleep(1);
    }

    sqlite3_close(g_psqlitedb);
    return NULL; // 配合线程退出返回 NULL
}

/*
 * updateData_systeminfo
 * - 功能: 将 stInData 结构的内容写回 systemInfo 表（包含 value/state/lib 字段）
 * - 参数: stsysteminfodata - 包含 name/value/state 的输入结构
 * - 说明: 此函数用于把外部结构转换为 SQL 并执行更新，使用锁保护 sqlite 操作
 */

void updateData_systeminfo(stInData stsysteminfodata)
{
    char updateSql[SQLDATALEN];
    char* errMsg;
    int rc;

    snprintf(updateSql, sizeof(updateSql), "UPDATE systemInfo SET value = '%s', state = '%c', lib = '0' WHERE name = '%s';",
             stsysteminfodata.value, stsysteminfodata.state[0], stsysteminfodata.name);

    sqlite3_busy_handler(g_psqlitedb, busyHandle, NULL);
    Lock(&sqlite3_mutex1, 0);
    
    rc = sqlite3_exec(g_psqlitedb, updateSql, NULL, 0, &errMsg);
    if (rc != SQLITE_OK) {
        // fprintf(stderr, "无法更新 systemInfo: %s\n", errMsg);
        sqlite3_free(errMsg);
    }

    Unlock(&sqlite3_mutex1);
}


/*
 * updateData_meshinfo
 * - 功能: 把 stInData 写入 meshInfo 表（与 updateData_systeminfo 类似）
 */

void updateData_meshinfo(stInData stmeshinfodata)
{
    char updateSql[SQLDATALEN];
    char* errMsg;
    int rc;

    snprintf(updateSql, sizeof(updateSql), "UPDATE meshInfo SET value = '%s', state = '%c', lib = '0' WHERE name = '%s';",
             stmeshinfodata.value, stmeshinfodata.state[0], stmeshinfodata.name);

    sqlite3_busy_handler(g_psqlitedb, busyHandle, NULL);
    Lock(&sqlite3_mutex1, 0);
    
    rc = sqlite3_exec(g_psqlitedb, updateSql, NULL, 0, &errMsg);
    if (rc != SQLITE_OK) {
        // fprintf(stderr, "无法更新 meshInfo: %s\n", errMsg);
        sqlite3_free(errMsg);
    }

    Unlock(&sqlite3_mutex1);
}
/*
 * updateData_userinfo
 * - 功能: 把用户相关参数写入 userInfo 表（例如串口/网络/其他用户可配置项）
 */
void updateData_userinfo(stInData stuserinfodata)
{
    char updateSql[SQLDATALEN];
    char* errMsg;
    int rc;

    snprintf(updateSql, sizeof(updateSql), "UPDATE userInfo SET value = '%s', state = '%c' WHERE name = '%s';",
             stuserinfodata.value, stuserinfodata.state[0], stuserinfodata.name);

    sqlite3_busy_handler(g_psqlitedb, busyHandle, NULL);
    Lock(&sqlite3_mutex1, 0);
    
    rc = sqlite3_exec(g_psqlitedb, updateSql, NULL, 0, &errMsg);
    if (rc != SQLITE_OK) {
        // fprintf(stderr, "无法更新 userInfo: %s\n", errMsg);
        sqlite3_free(errMsg);
    }

    Unlock(&sqlite3_mutex1);
}



/* ========================================================
 * 第二部分：文件解析函数 
 * ======================================================== */
void updateData_init(void){
stInData stsysteminfodata;

		memset((char*)&stsysteminfodata,0,sizeof(stsysteminfodata));
		sprintf(stsysteminfodata.name,"%s","rf_freq");
		sprintf(stsysteminfodata.value,"%d",FREQ_INIT);
		stsysteminfodata.state[0] = '0';
		updateData_meshinfo(stsysteminfodata);

		memset((char*)&stsysteminfodata,0,sizeof(stsysteminfodata));
		sprintf(stsysteminfodata.name,"%s","m_chanbw");
		sprintf(stsysteminfodata.value,"%d",BW_INIT);
		stsysteminfodata.state[0] = '0';
		updateData_meshinfo(stsysteminfodata);

		memset((char*)&stsysteminfodata,0,sizeof(stsysteminfodata));
		sprintf(stsysteminfodata.name,"%s","m_txpower");
		sprintf(stsysteminfodata.value,"%d",POWER_INIT);
		stsysteminfodata.state[0] = '0';
		updateData_meshinfo(stsysteminfodata);

		memset((char*)&stsysteminfodata,0,sizeof(stsysteminfodata));
		sprintf(stsysteminfodata.name,"%s","workmode");
		sprintf(stsysteminfodata.value,"%d",NET_WORKMOD_INIT);
		stsysteminfodata.state[0] = '0';
		updateData_meshinfo(stsysteminfodata);
		
		memset((char*)&stsysteminfodata,0,sizeof(stsysteminfodata));
		sprintf(stsysteminfodata.name,"%s","devicetype");
		sprintf(stsysteminfodata.value,"%d",DEVICETYPE_INIT);
		stsysteminfodata.state[0] = '0';
		updateData_meshinfo(stsysteminfodata);
}

/*
 * updateData_init
 * - 功能: 程序启动时把内存中的初始静态参数写入数据库（例如初始频率、带宽、功率、工作模式等）
 * - 说明: 这些初始值来源于全局变量（FREQ_INIT/BW_INIT/POWER_INIT/...），用于使 UI/数据库与运行态保持一致
 */
void read_node_xwg_file(const char* filename,Node_Xwg_Pairs* xwg_info,int num_pairs)
{
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("无法打开文件");
        return;
    }   
    for (int i = 0; i < num_pairs; i++) xwg_info[i].found = 0;

    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = 0;
        char *key = strtok(line, " ");
        char *value_str = strtok(NULL, " ");
        if (key != NULL && value_str != NULL) {
            for (int i = 0; i < num_pairs; i++) {
                if (strcmp(key, xwg_info[i].key) == 0) {
                    memcpy(xwg_info[i].value,value_str,100);
                    xwg_info[i].found = 1;
                    break;
                }
            }
        }
    }
    fclose(file);   
}

/*
 * read_node_xwg_file
 * - 功能: 读取 /etc/node_xwg 或类似格式的文件，将 key value 对解析到 Node_Xwg_Pairs 数组
 * - 参数: filename   - 要读取的文件路径
 *           xwg_info  - 目标数组，保存 key/value/found 标志
 *           num_pairs - 数组元素数量（迭代上限）
 */

const char *get_value(Node_Xwg_Pairs* pairs,const char *key) {
    for (int i = 0; i < 20; i++) { // 假设 MAX_XWG_PAIRS = 20
        if (strcmp(key, pairs[i].key) == 0 && pairs[i].found) {
            return pairs[i].value;
        }
    }
    return NULL; 
}

/*
 * get_value
 * - 功能: 从 Node_Xwg_Pairs 数组中根据 key 找到对应 value，并返回指针
 * - 返回: 如果找到且 found 为真则返回 value 指针，否则返回 NULL
 */

double get_double_value(Node_Xwg_Pairs* pairs,const char *key) {
    const char *value = get_value(pairs,key);
    if (value != NULL) return atof(value);
    return 0.0;
}

/*
 * get_double_value
 * - 功能: 获取 key 的值并按 double 返回（未找到返回 0.0）
 */

int get_int_value(Node_Xwg_Pairs* pairs,const char *key) {
    const char *value = get_value(pairs,key);
    if (value != NULL) return atoi(value);
    return -1;
}

/*
 * get_int_value
 * - 功能: 获取 key 的值并按 int 返回（未找到返回 -1）
 */