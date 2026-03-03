#include "Thread.h"
#include <unistd.h>

pthread_t Create_Thread(void (pFun)(void *),void *arg)
{
	pthread_t thread_tid = 0;
	void *(*pLinuxFun)(void *);
	pLinuxFun = (void* (*)(void*))pFun;
	pthread_create(&thread_tid,NULL,pLinuxFun,arg);
	return thread_tid;
}

/*
 * Create_Thread
 * 参数：
 *  - pFun: 线程入口函数，签名为 void func(void*)
 *  - arg: 传给线程入口的参数指针
 * 返回：成功创建线程返回 pthread_t（线程 id），失败返回 0 或未定义值
 * 说明：以默认属性创建一个 pthread，适用于不需要特殊调度策略的后台任务。
 */

pthread_t Create_ThreadAndPriority(INT32 priority,void (pFun)(void *),void *arg)
{
	pthread_t thread_tid = 0;
	pthread_attr_t prior;
	struct sched_param param;
	void *(*pLinuxFun)(void *);
	pLinuxFun = (void* (*)(void*))pFun;
	if(priority == 0)
	{
		pthread_create(&thread_tid,NULL,pLinuxFun,arg);
		return thread_tid;
	}
	else
	{
		pthread_attr_init(&prior);
		pthread_attr_getschedparam(&prior,&param);
		param.sched_priority = priority;
		pthread_attr_setschedpolicy(&prior,SCHED_RR);
		pthread_attr_setschedparam(&prior,&param);
		pthread_create(&thread_tid,&prior,pLinuxFun,arg);
	}
}

/*
 * Create_ThreadAndPriority
 * 参数：
 *  - priority: 优先级（0 表示使用默认创建方式）
 *  - pFun: 线程入口函数
 *  - arg: 线程参数
 * 返回：创建的 pthread_t
 * 说明：当 priority 非 0 时，尝试使用实时调度策略 SCHED_RR 并设置优先级。
 * 注意：设置实时优先级可能需要特权（CAP_SYS_NICE）或调整系统限制。
 */


INT32 Close_Thread(pthread_t threadid)
{
	if(threadid != 0)
	{
		pthread_join(threadid,NULL);
		return RETURN_OK;
	}
	return RETURN_FAILED;
}

/*
 * Close_Thread
 * 参数：threadid - 要 join 的线程 id
 * 返回：RETURN_OK 表示 join 成功，RETURN_FAILED 表示传入无效 id
 * 说明：通过 pthread_join 等待线程结束并回收资源，适用于需要同步结束的场景。
 */

void ThreadSleep(INT32 tparam)
{
	usleep(tparam*1000);
}

/*
 * ThreadSleep
 * 参数：tparam - 毫秒级睡眠时长
 * 说明：将毫秒转换为微秒调用 usleep，便于线程以毫秒粒度短时等待。
 */

//int main()
//{
//	return 0;
//}
