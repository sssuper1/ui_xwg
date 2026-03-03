#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "Thread.h"
#include "ui_get.h"
#include "sqlite_unit.h"
#include <signal.h>
#include "Thread.h"
#include "mgmt_transmit.h"
// 引入你 mock 的其他头文件

/*
 * main.c
 * 说明：程序入口，负责系统初始化、串口初始化、SQLite 初始化以及启动后台线程。
 * 线程分工：
 *  - `sqlite_set_param`：后台定时检查数据库变更并下发网络参数/持久化数据库；
 *  - `get_ui_Thread`：读取并解析来自 UI（串口）的命令帧（如 0x04、0x08）；
 *  - `write_ui_Thread`：周期性（如每秒）向 UI 发送状态帧（0x05、0x06、0x07）。
 */

int ui_fd;

/*
 * SetupSignal
 * 说明：设置进程信号处理，忽略 SIGPIPE 以避免在向已经关闭的 socket/串口写入时进程被意外终止。
 */
void SetupSignal()
{
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    if(sigemptyset(&sa.sa_mask) == -1 ||
            sigaction(SIGPIPE,&sa,0) == -1)
    {
        exit(-1);
    }
}

int main() {
    printf("I.MX6ULL UI Controller Starting...\n");

    /*
     * 启动流程：
     * 1) 忽略 SIGPIPE，避免写已关闭的 fd 导致进程退出；
     * 2) 调用 `mgmt_system_init` 完成系统与网络套接字初始化；
     * 3) 初始化 UI 串口（`uart_init`），并创建处理线程；
     * 4) 初始化 sqlite，并启动数据库轮询线程用于应用参数变更。
     */
    SetupSignal();
    mgmt_system_init();
    
    ui_fd = uart_init();
    if(ui_fd == -1) {
        printf("Init UI UART Error. Please check /dev/ttymxcX\n");
        return -1;
    }

    /* 初始化 sqlite 并创建后台线程 */
    sqliteinit();
    /* 后台线程：周期性检查并应用 DB 中的 set 操作 */
    Create_Thread(sqlite_set_param, NULL);
    Create_Thread((void*)mgmt_get_msg, NULL);
    /* 串口读取线程：解析来自触摸屏/上位机的命令 */
    Create_Thread(get_ui_Thread, (void*)&ui_fd);
    /* 串口写线程：定期向屏幕上报状态 */
    Create_Thread(write_ui_Thread, (void*)&ui_fd);

    /* 主线程阻塞，所有业务在子线程中进行 */
    while(1) {
        sleep(10);
    }

    return 0;
}