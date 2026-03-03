#ifndef MGMT_NETLINK_H_
#define MGMT_NETLINK_H_

//#include <netlink/genl/genl.h>
//#include <netlink/genl/ctrl.h>

#include "mgmt_types.h"

extern Smgmt_transmit_info mgmt_info;

extern ob_state_part1 slot_info;
/*
 * mgmt_netlink_set_param
 * 参数：
 *  - buf: 指向包含 Smgmt_header + payload 的缓冲区（通常 host 或 network 字节序由调用方负责）
 *  - len: 缓冲区长度
 *  - ifname: 可选的网卡名，若为 NULL 则使用默认接口
 * 返回：0 表示成功，非 0 表示失败（具体值取决于实现）
 * 说明：将管理参数下发给底层（例如通过 netlink 或驱动接口），实现细节平台相关。
 */
int mgmt_netlink_set_param(char *buf, int len, char *ifname);

/*
 * mgmt_netlink_get_info
 * 参数：
 *  - type, cmd: 查询类型与子命令（由 mgmt_types.h 中的枚举定义）
 *  - ifname: 可选网卡名
 *  - out_buf: 输出缓冲区（由调用方分配）
 * 返回：填充到 out_buf 的字节数或错误码
 * 说明：用于从底层获取当前设备或驱动的状态信息。
 */
int mgmt_netlink_get_info(int type, int cmd, char *ifname, char *out_buf);
#endif /* MGMT_NETLINK_H_ */
