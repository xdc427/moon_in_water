#ifndef _MOON_THREAD_INFO_H_
#define _MOON_THREAD_INFO_H_
#include<pthread.h>
#include<sys/time.h>

//这是一个获取线程状态的接口
//决定是否调度新的新的线程来执行当前任务
//开启新线程的模块可以动态提高或降低线程层级，其他使用模块只能读取

typedef enum{
	//timer, listen_task应该是0这个级别，在这个级别下的线程不应当执行任何耗时任务
	THREAD_LEVEL0,
	//dns，以及新开的需要遍历处理的线程 应该在1-6这个级别
	THREAD_LEVEL1,
	THREAD_LEVEL2,
	THREAD_LEVEL3,
	THREAD_LEVEL4,
	THREAD_LEVEL5,
	THREAD_LEVEL6,
	//独个任务，可以被独占来处理任何耗时任务的线程
	THREAD_LEVEL7
} thread_info_level_e;

typedef struct thread_info_s{
	thread_info_level_e level;
	struct timeval tv_start;
	struct timeval tv_expect_end;
} thread_info_s;
typedef thread_info_s * thread_info;

int set_thread_info( thread_info p_info );
thread_info get_thread_info();
thread_info init_thread();

#endif

