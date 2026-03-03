#ifndef SQLITE_UNIT_H_
#define SQLITE_UNIT_H_

#include <stdbool.h>
#include "mgmt_types.h"


#define SYSTEMINFO_PARAM_NUM 105
#define SQLDATALEN           1024
extern uint8_t  rate_auto;
extern uint8_t BW_INIT;
extern uint16_t POWER_INIT;
extern uint8_t NET_WORKMOD_INIT;
extern uint8_t DEVICETYPE_INIT;
void mgmt_system_init();
int sqliteinit(void);

void* sqlite_set_param(void* arg);
bool persist_test_db(void);
void updateData_systeminfo_qk(const char* key, int value); 
void updateData_meshinfo_qk(const char* key, int value);
void read_node_xwg_file(const char* filename,Node_Xwg_Pairs* xwg_info,int num_pairs);
const char *get_value(Node_Xwg_Pairs* pairs,const char *key) ;
int get_int_value(Node_Xwg_Pairs* pairs,const char *key) ;
double get_double_value(Node_Xwg_Pairs* pairs,const char *key);
void updateData_init(void);

/*
 * sqlite_unit.h 注释：
 * - sqliteinit: 初始化 sqlite3 数据库连接与必要的表/回调。
 * - sqlite_set_param: 后台线程函数，轮询 DB 并对变更执行 mgmt 下发或组播。
 * - persist_test_db: 将当前 test.db 原子地复制到网页可访问路径并校验完整性，返回 true/false。
 * - updateData_systeminfo_qk/updateData_meshinfo_qk: 快速更新 systeminfo/meshinfo 中的整数项（简写版）。
 * - read_node_xwg_file/get_value/get_int_value/get_double_value: 解析本地配置文件 node_xwg 并返回键值。
 * - updateData_init: 用于初始化或重置数据库中某些字段（实现内定义）。
 */

#endif /* SQLITE_UNIT_H_ */
