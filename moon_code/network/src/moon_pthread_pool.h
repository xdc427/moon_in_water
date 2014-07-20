#ifndef _MOON_PTHREAD_POOL_H_
#define _MOON_PTHREAD_POOL_H_
#include"moon_interface.h"

#define FUNC_PARA_NUM 4
typedef struct{
	int ( *task_func )( common_user_data p_user_data );
	common_user_data_u user_data[ FUNC_PARA_NUM ];
} process_task_s;
typedef process_task_s * process_task;

void * get_pthreadpool_instance();

typedef struct{
	int ( *put_task )( void * p_pool, process_task p_task );
	void ( *get_task_info )( void * p_ppool, int * task_num, int * process_num );
} pthreadpool_interface_s;
typedef pthreadpool_interface_s * pthreadpool_interface;
DECLARE_INTERFACE( pthreadpool_interface_s );

#endif

