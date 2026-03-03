#ifndef SOCKETUDP_H_
#define SOCKETUDP_H_

#include <netinet/in.h>    // for sockaddr_in
#include <sys/types.h>    // for socket
#include <sys/socket.h>    // for socket
#include <stdio.h>        // for printf
#include <stdlib.h>        // for exit
#include <string.h>        // for bzero
#include <math.h>
#include <arpa/inet.h>

int CreateUDPServer(int port);
/*
 * CreateUDPServer
 *  - 在本地绑定 UDP 端口并返回 socket fd，失败返回 <= 0
 */
int CreateUDPServer(int port);

/*
 * RecvUDPClient
 *  - 从指定 socket 接收 UDP 数据，填充 buf 并将源地址写入 from
 *  - 返回接收到的字节数，失败返回负值
 */
int RecvUDPClient(int socket, char *buf, int bufsize, struct sockaddr_in *from,
	socklen_t *from_len);

/*
 * SendUDPClient
 *  - 通过给定 socket 向目的地址 `to` 发送 `msg` 长度为 `len` 的数据
 *  - 返回发送的字节数或错误码
 */
int SendUDPClient(int socket, char *msg, int len, struct sockaddr_in * to);

/*
 * MakeCMD
 *  - 辅助函数，组合或转换命令字符串（项目内部用途，具体语义取决于实现）
 */
int MakeCMD(char *buf1, char *buf2);

/*
 * CreateUDPServerToDevice
 *  - 将 socket 绑定到指定设备（如网卡名），用于下发或接收按设备过滤的 UDP
 */
int CreateUDPServerToDevice(char* eth, int len, int port);

/*
 * CloseUDPSocket
 *  - 关闭并清理 UDP socket
 */
void CloseUDPSocket(int workSockfd);

/*
 * createUdpClient
 *  - 初始化并填充 `addr` 结构以发送到指定 IP/端口，返回 socket fd（或状态）
 */
int createUdpClient(struct sockaddr_in *addr,const char* ip,const int port);

/*
 * add_multiaddr_group
 *  - 将 socket 加入多播组 group_ip
 */
void add_multiaddr_group(int s,char* group_ip);

#endif /* SOCKETUDP_H_ */
