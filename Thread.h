#ifndef   _THREAD_H
#define   _THREAD_H

#include "mgmt_types.h"
#include <pthread.h>
//#include <process.h>

/* 线程辅助接口说明：
 * - Create_Thread: 以默认属性创建线程，入口函数签名为 void func(void*), 返回 pthread_t。
 * - Create_ThreadAndPriority: 可指定优先级创建线程（priority==0 表示使用默认属性）。
 * - Close_Thread: 使用 pthread_join 等待并回收线程，返回 RETURN_OK/RETURN_FAILED。
 * - ThreadSleep: 毫秒级睡眠辅助函数。
 */
pthread_t Create_Thread(void (pFun)(void *),void *arg);
pthread_t Create_ThreadAndPriority(INT32 priority,void (pFun)(void *),void *arg);
INT32 Close_Thread(pthread_t threadid);
void ThreadSleep(INT32 tparam);

#endif
