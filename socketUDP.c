/*
 * socketUDP.c
 *
 *  Created on: Jan 26, 2021
 *      Author: slb
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE  // 必须放在最前面
#endif

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#include "mgmt_types.h"
#include "socketUDP.h"

int CreateUDPServer(int port)
{
	int server_sockfd;
	struct sockaddr_in my_addr;   //服务器网络地址结构体
	memset(&my_addr, 0, sizeof(my_addr)); //数据初始化--清零
	my_addr.sin_family = AF_INET; //设置为IP通信
	my_addr.sin_addr.s_addr = INADDR_ANY;//服务器IP地址--允许连接到所有本地地址上
	my_addr.sin_port = htons(port); //服务器端口号
	if ((server_sockfd = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
	{
		perror("socket");
		return -1;
	}

	int reuse = 1;
    if(setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
    }	
	/*将套接字绑定到服务器的网络地址上*/
	if (bind(server_sockfd, (struct sockaddr*)&my_addr, sizeof(struct sockaddr)) < 0)
	{
		perror("bind");
		return -1;
	}

	return server_sockfd;
}

/*
 * CreateUDPServer
 * - 功能: 在本地任意地址上创建并绑定一个 UDP 服务器套接字
 * - 参数: port - 要绑定的本地端口
 * - 返回: >=0 的 socket fd 成功，-1 失败
 * - 说明: 会设置 SO_REUSEADDR 允许端口重用
 */

int CreateUDPServerToDevice(char* eth, int len, int port)
{
	int workSockfd;
	struct sockaddr_in svrAddr;
	int flag;

	svrAddr.sin_family = AF_INET;
	svrAddr.sin_addr.s_addr = INADDR_ANY;
	svrAddr.sin_port = htons(port);

	workSockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (workSockfd <= 0)
	{
		return RETURN_FAILED;
	}

	if (/*RETURN_FAILED*/-1 == bind(workSockfd, (struct sockaddr*)&svrAddr, sizeof(svrAddr)))
	{
		CloseUDPSocket(workSockfd);
		return RETURN_FAILED;
	}
	flag = 1;

	if (setsockopt(workSockfd, SOL_SOCKET, SO_BINDTODEVICE, eth, len) == -1)
	{
		//		printf("setsockopt SO_BINDTODEVICE error! 2\n");
		CloseUDPSocket(workSockfd);
		return RETURN_FAILED;
	}

	return workSockfd;
}

/*
 * CreateUDPServerToDevice
 * - 功能: 创建 UDP 套接字并绑定到指定设备（eth），用于发送/接收绑定到某个网口的数据
 * - 参数: eth - 接口名字符串（如 "eth0"）
 *           len - 接口名长度
 *           port - 本地绑定端口
 * - 返回: socket fd 成功，RETURN_FAILED 失败
 * - 说明: 使用 SO_BINDTODEVICE 将 socket 绑定到网口
 */

int RecvUDPClient(int socket, char* buf, int bufsize, struct sockaddr_in* from, socklen_t* from_len)
{
	int len;
	//	printf("waiting for a packet...\n");
	if ((len = recvfrom(socket, buf, bufsize, 0, from, from_len)) < 0)
	{
		//printf("receive error");
		return -1;
	}
	//	printf("received packet from %d %d\n",from->sin_addr,from->sin_addr.s_addr);
	buf[len] = '\0';
	//	printf("contents: %s\n",buf);
	return len;
}

/*
 * RecvUDPClient
 * - 功能: 从 UDP socket 接收数据，封装 recvfrom
 * - 参数: socket - socket fd
 *           buf - 接收缓冲
 *           bufsize - 缓冲长度
 *           from - 输出发送方地址
 *           from_len - 地址长度指针
 * - 返回: 实际接收字节数，负值表示错误
 */

int SendUDPClient(int socket, char* msg, int len, struct sockaddr_in* to)
{
	int s_len;
	if ((s_len = sendto(socket, msg, len, 0, to, sizeof(struct sockaddr))) != len)
	{
		fprintf(stderr, "send failed: %s,ErrNo:%d\n", strerror(errno),errno);
		return -1;
	}
		
	return s_len;
}

/*
 * SendUDPClient
 * - 功能: 通过指定 socket 向目标地址发送 UDP 报文
 * - 参数: socket - socket fd
 *           msg - 发送数据缓冲
 *           len - 数据长度
 *           to  - 目标地址 (struct sockaddr_in*)
 * - 返回: 发送的字节数，负值表示失败
 */


//add by yang
int SendUDPBrocast(int socket, char* msg, int len, struct sockaddr_in* to)
{
	int ret;
	int broadcast = 1;

	// Send the message
	ret = sendto(socket, msg, len, 0, to, sizeof(struct sockaddr_in));
	if (ret < 0) {
		perror("sendto() failed");
		return -1;
	}

	return 0;
}

/*
 * SendUDPBrocast
 * - 功能: 发送广播数据（与 SendUDPClient 类似，但用于广播）
 * - 参数: socket,msg,len,to
 * - 返回: 0 成功，-1 失败
 */


void CloseUDPSocket(int workSockfd)
{
	if (workSockfd > 0)
	{
		shutdown(workSockfd, SHUT_RDWR);
		close(workSockfd);
	}
}

/*
 * CloseUDPSocket
 * - 功能: 关闭并释放 UDP socket
 * - 参数: workSockfd - 要关闭的 socket fd
 */

//add by sdg 
int createUdpClient(struct sockaddr_in *addr,const char* ip,const int port)
{
	int s=socket(AF_INET, SOCK_DGRAM, 0);

	addr->sin_family = AF_INET;
	addr->sin_port =htons(port);

	if(inet_pton(AF_INET, ip, &addr->sin_addr) <= 0)
	{
		perror("invalid address \r\n");	
		return -1;	
	}

	return s;
}

/*
 * createUdpClient
 * - 功能: 创建一个 UDP client socket，并初始化提供的 sockaddr_in 结构（但不 connect）
 * - 参数: addr - 输出的 sockaddr_in 结构指针
 *           ip - 目标 IP 字符串（用于设置 addr->sin_addr）
 *           port - 目标端口（用于设置 addr->sin_port）
 * - 返回: socket fd（>0）成功，-1 失败
 */

/* 加入到组播 */
void add_multiaddr_group(int s,char* group_ip)
{
	struct ip_mreq ldyw_group;
	ldyw_group.imr_multiaddr.s_addr = inet_addr(group_ip);
	ldyw_group.imr_interface.s_addr = htonl(INADDR_ANY);

	if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, &ldyw_group, sizeof(ldyw_group)) < 0) {
        printf("[CJ DEBUG] :fail to Join multicast group \n");
        //join_success = 1;
    } 

}

/*
 * add_multiaddr_group
 * - 功能: 将 socket 加入到指定的组播组
 * - 参数: s - socket fd
 *           group_ip - 组播地址字符串（例如 "239.255.255.255"）
 * - 说明: 使用 IP_ADD_MEMBERSHIP
 */
