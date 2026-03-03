#ifndef   _LOCK_H 
#define   _LOCK_H 

//#include <stdef.h>
//#include "StdAfx.h"
//#include <stdef.h>
#include "mgmt_types.h"
#include <pthread.h>
#include <time.h>
#include <errno.h>


//#define TIME_WAIT_FOR_EVER 0;
//xian cheng shuo
/* 简要说明：
 * - CreateLock: 返回已初始化的 pthread_mutex_t（按值返回，调用方保存副本）。
 * - CreateEvent: 返回已初始化的 pthread_cond_t（按值返回，调用方保存副本）。
 * - Lock: 尝试对 mutex 加锁，passMicroSeconds 为超时（毫秒），0 表示阻塞等待。
 * - Unlock: 释放 mutex。
 * - GetEvent: 等待条件变量（可与 Lock 配合使用），返回 WAIT/OK/FAIL。
 * - SetEvent: 通知条件变量，唤醒等待线程。
 */
pthread_mutex_t CreateLock();
pthread_cond_t CreateEvent();
INT32 Lock(pthread_mutex_t* mutex,INT32 passMicroSeconds);
INT32 Unlock(pthread_mutex_t* mutex);
INT32 GetEvent(pthread_cond_t* cond,pthread_mutex_t* mutex);
INT32 SetEvent(pthread_cond_t* cond);

#endif
