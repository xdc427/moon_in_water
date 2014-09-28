#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include"moon_thread_info.h"
#include"moon_pthread_pool.h"
#include"moon_common.h"

static int func( common_user_data p_user_data )
{
	thread_info p_info;;

	sleep( 1 );
	p_info = get_thread_info( );
	if( p_info != NULL ){
		MOON_PRINT_MAN( ERROR, "not set!" );		
	}
	p_info = malloc( sizeof( *p_info ) );
	p_info->level = p_user_data->i_num;
	set_thread_info( p_info );
	p_info = get_thread_info( );
	if( p_info->level == p_user_data->i_num ){
		MOON_PRINT( TEST, NULL, "ok:%d", p_info->level );
	}else{
		MOON_PRINT( TEST, NULL, "set error!" );
	}
	return 0;
}

int main()
{
	void * p_ppool;
	pthreadpool_interface p_ppool_i;
	process_task_s task;
	int i;

	p_ppool = get_pthreadpool_instance();
	p_ppool_i = FIND_INTERFACE( p_ppool, pthreadpool_interface_s );
	task.task_func = func;
	for( i = 0; i < 10; i++ ){
		task.user_data[ 0 ].i_num = i;
		p_ppool_i->put_task( p_ppool, &task );
	}
	sleep( 10 );
	return 0;
}

