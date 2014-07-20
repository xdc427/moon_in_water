#include<stdio.h>
#include<pthread.h>
#include"moon_debug.h"
#include"moon_pthread_pool.h"

static const char xid[] = "pthread_pool_test";
static const char xid_error[] = "pthread_pool_test_error";
static const char xid_info[]  = "pthread_pool_test_info";
static int task_index = 0;
static int task_num = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t sync_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sync_cond = PTHREAD_COND_INITIALIZER;

static int get_task_index( )
{
	return __sync_fetch_and_add( &task_index, 1 );
}

static void task_num_inc( )
{
	pthread_mutex_lock( &sync_mutex );
	task_num++;
	pthread_mutex_unlock( &sync_mutex );
}

static void task_num_dec()
{
	pthread_mutex_lock( &sync_mutex );
	task_num--;
	if( task_num == 0 ){
		pthread_cond_signal( &sync_cond );
	}
	pthread_mutex_unlock( &sync_mutex );
}


//para0:task_num
static void test_func0( common_user_data p_user_data )
{
	task_num_dec();
	MOON_PRINT( TEST, xid, "0:inprocess:%d", p_user_data->i_num );
}
//para0:task_num, para1:last_task_num
static void test_func1( common_user_data p_user_data )
{
	void * p_ppool;
	pthreadpool_interface p_pp_i;
	process_task_s task;

	p_ppool = get_pthreadpool_instance();
	p_pp_i = FIND_INTERFACE( p_ppool, pthreadpool_interface_s );
	MOON_PRINT( TEST, xid, "1:inprocess:%d", p_user_data->i_num );
	if( p_user_data[ 1 ].i_num != 0 ){
		task.user_data[ 0 ].i_num = get_task_index();
		task.user_data[ 1 ].i_num = p_user_data[ 1 ].i_num - 1;
		task.task_func = test_func1;
		if( p_pp_i->put_task( p_ppool, &task ) >= 0 ){
			MOON_PRINT( TEST, xid, "1:preprocess:%d", task.user_data[ 0 ].i_num );
		}else{
			task_num_dec();
			MOON_PRINT( TEST, xid_error, "func1:%d:%d"
				, p_user_data[ 0 ].i_num, p_user_data[ 1 ].i_num );
		}
	}else{
		task_num_dec();
	}
}

//para0:task_num, para1:last_task_num
static void test_func2( common_user_data p_user_data )
{
	void * p_ppool;
	pthreadpool_interface p_pp_i;
	process_task_s task;

	p_ppool = get_pthreadpool_instance();
	p_pp_i = FIND_INTERFACE( p_ppool, pthreadpool_interface_s );
	pthread_mutex_lock( &mutex );
	MOON_PRINT( TEST, xid, "2:inprocess:%d", p_user_data->i_num );
	if( p_user_data[ 1 ].i_num != 0 ){
		task.user_data[ 0 ].i_num = get_task_index();
		task.user_data[ 1 ].i_num = p_user_data[ 1 ].i_num - 1;
		task.task_func = test_func2;
		if( p_pp_i->put_task( p_ppool, &task ) >=  0 ){
			MOON_PRINT( TEST, xid, "2:preprocess:%d", task.user_data[ 0 ].i_num );
		}else{
			task_num_dec();
			MOON_PRINT( TEST, xid_error, "func2:%d:%d"
				, p_user_data[ 0 ].i_num, p_user_data[ 1 ].i_num );
		}
	}else{
		task_num_dec();
	}
	pthread_mutex_unlock( &mutex );
}

static void ( *test_funcs[] )( common_user_data ) = { test_func0, test_func1, test_func2 };

int main( int argc, char * argv[] )
{
	void * p_ppool;
	pthreadpool_interface p_pp_i;
	process_task_s task;
	int i, j;
	int tnum, pnum, repeat_num;

	if( argc < 2 ){
		return 0;
	}
	repeat_num = atoi( argv[ 1 ] );
	p_ppool = get_pthreadpool_instance();
	p_pp_i = FIND_INTERFACE( p_ppool, pthreadpool_interface_s );

	task_num_inc();
	for( i = 0; i < repeat_num; i++ ){
		p_pp_i->get_task_info( p_ppool, &tnum, &pnum );
		MOON_PRINT( TEST, xid_info, "%d:%d", tnum, pnum );
		for( j = 0; j < ARRAY_LEN( test_funcs ); j++ ){
			task.user_data[ 0 ].i_num = get_task_index();
			task.user_data[ 1 ].i_num = 3;
			task.task_func = test_funcs[ j ];
			if( p_pp_i->put_task( p_ppool, &task ) >= 0 ){
				task_num_inc();
				MOON_PRINT( TEST, xid, "%d:preprocess:%d", j, task.user_data[ 0 ].i_num );
			}else{
				MOON_PRINT( TEST, xid_error, "main:%d", task.user_data[ 0 ].i_num );
			}
		}
	}
	task_num_dec();
	pthread_mutex_lock( &sync_mutex );
	if( task_num != 0 ){
		pthread_cond_wait( &sync_cond, &sync_mutex );
	}
	pthread_mutex_unlock( &sync_mutex );
	return 0;
}

