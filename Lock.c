#include "Lock.h"

pthread_mutex_t CreateLock()
{
	pthread_mutex_t mutex;
	pthread_mutex_init(&mutex,NULL);
	return mutex;
}

/*
 * CreateLock
 * - 功能: 初始化并返回一个 pthread_mutex_t（按值返回）
 * - 返回: 初始化后的互斥量
 * - 注意: 返回的是按值拷贝的 mutex，调用方通常将其赋值给全局或局部变量
 */

INT32 Lock(pthread_mutex_t* mutex,INT32 passMicroSeconds)
{
	INT32 waitResult;
	if(passMicroSeconds == 0)
	{
		waitResult = pthread_mutex_lock(mutex);
	}
	else
	{
		struct timespec abs_timeout;
		abs_timeout.tv_sec = passMicroSeconds;
		abs_timeout.tv_nsec = 0;
		waitResult = pthread_mutex_timedlock(mutex,&abs_timeout);
	}
	switch(waitResult)
	{
	case 0:
		return RETURN_OK;
	case ETIMEDOUT:
		return RETURN_TIMEOUT;
	default:
		return RETURN_FAILED;
	} 
}

/*
 * Lock
 * - 功能: 对传入的 mutex 进行上锁
 * - 参数: mutex - 指向 pthread_mutex_t
 *           passMicroSeconds - 如果为 0 则阻塞直到获得锁，否则使用 timedlock（以秒为单位）
 * - 返回: RETURN_OK/RETURN_TIMEOUT/RETURN_FAILED
 */


INT32 Unlock(pthread_mutex_t* mutex)
{
	 return pthread_mutex_unlock(mutex);
}

/*
 * Unlock
 * - 功能: 解锁指定 mutex
 * - 参数: mutex - 指向 pthread_mutex_t
 * - 返回: pthread_mutex_unlock 的返回值（0 表示成功）
 */

pthread_cond_t CreateEvent()
{
	pthread_cond_t cond;
	pthread_cond_init(&cond,NULL);
	return cond;
}

/*
 * CreateEvent
 * - 功能: 初始化并返回一个条件变量（pthread_cond_t）
 */

INT32 GetEvent(pthread_cond_t* cond,pthread_mutex_t* mutex)
{
	int i = pthread_cond_wait(cond,mutex);
	return i;
}

/*
 * GetEvent
 * - 功能: 等待条件变量（阻塞）
 * - 参数: cond - 指向条件变量
 *           mutex - 与条件变量配套的 mutex（必须已上锁）
 * - 返回: pthread_cond_wait 返回值
 */

INT32 SetEvent(pthread_cond_t* cond)
{
	int i = pthread_cond_broadcast(cond);
	return i;
}

/*
 * SetEvent
 * - 功能: 广播唤醒所有等待在 cond 上的线程
 */
