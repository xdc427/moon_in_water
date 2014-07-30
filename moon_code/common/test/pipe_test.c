#include<stdio.h>
#include<pthread.h>
#include<unistd.h>
#include"moon_pipe.h"
#include"common_interfaces.h"
#include"moon_pthread_pool.h"
#include"moon_debug.h"

typedef struct{
	void ( *echo )( void *, void * );
} test_interface_s;
DECLARE_INTERFACE( test_interface_s );

typedef struct{
	int a;
} pipe_test_data_s;

typedef struct{
	int ref_num;
	int status;
	pipe_test_data_s * p_pipe;
	pthread_mutex_t mutex;
} test_internal_s;

static void test_ref_inc( void * p_data );
static void test_ref_dec( void * p_data );
static void test_close( void * p_data, void * p_pipe_data );
static void test_echo( void * p_data, void * p_pipe );

STATIC_BEGAIN_INTERFACE( test_hub )
STATIC_DECLARE_INTERFACE( test_interface_s )
STATIC_DECLARE_INTERFACE( gc_interface_s )
STATIC_DECLARE_INTERFACE( pipe_listener_interface_s )
STATIC_END_DECLARE_INTERFACE( test_hub, 3 )
STATIC_GET_INTERFACE( test_hub, test_interface_s, 0 ) = {
	.echo = test_echo
}
STATIC_GET_INTERFACE( test_hub, pipe_listener_interface_s, 1 ) = {
	.close = test_close
}
STATIC_GET_INTERFACE( test_hub, gc_interface_s, 2 ) = {
	.ref_inc = test_ref_inc,
	.ref_dec = test_ref_dec
}
STATIC_END_INTERFACE( NULL )

static void _test_func1( common_user_data p_user_data )
{
	void * p_pipe;
	pipe_interface p_pipe_i;
	gc_interface p_gc_i;
	void * p_point, * p_point_data;
	int ret;

	p_pipe = p_user_data->ptr;
	p_pipe_i = FIND_INTERFACE( p_pipe, pipe_interface_s );
	p_gc_i = FIND_INTERFACE( p_pipe, gc_interface_s );
	MOON_PRINT( TEST, NULL, "begain get ref" );
	ret = p_pipe_i->get_other_point_ref( p_pipe, &p_point, &p_point_data );
	MOON_PRINT( TEST, NULL, "end get ref:%d", ret );
	//p_point is NULL ,don't free
	CALL_INTERFACE_FUNC( p_point_data, gc_interface_s, ref_dec );
	p_gc_i->ref_dec( p_pipe );	
}

static void test_func1()
{
	void * p_pipe[ 2 ];
	void * p_ppool;
	pipe_interface p_pipe_i;
	gc_interface p_gc_i;
	pthreadpool_interface p_pp_i;
	process_task_s task;
	int i;

	p_ppool = get_pthreadpool_instance();
	p_pp_i = FIND_INTERFACE( p_ppool, pthreadpool_interface_s );
	pipe_new( &p_pipe, 0, 0, 1 );
	p_pipe_i = FIND_INTERFACE( p_pipe[ 0 ], pipe_interface_s );
	p_gc_i = FIND_INTERFACE( p_pipe[ 0 ], gc_interface_s );
	
	task.user_data[ 0 ].ptr = p_pipe[ 0 ];
	task.task_func = _test_func1;
	for( i = 0; i < 100; i++ ){
		MOON_PRINT( TEST, NULL, "put task" );
		p_gc_i->ref_inc( p_pipe[ 0 ] );
		p_pp_i->put_task( p_ppool, &task );
	}
	//sleep( 10 );
	MOON_PRINT( TEST, NULL, "init done" );
	p_pipe_i->init_done( p_pipe[ 0 ], 0 );
	for( i = 0; i < 100; i++ ){
		MOON_PRINT( TEST, NULL, "put task" );
		p_gc_i->ref_inc( p_pipe[ 0 ] );
		p_pp_i->put_task( p_ppool, &task );
	}
	sleep( 10 );
	p_pipe_i->close( p_pipe[ 0 ] );
	p_gc_i->ref_dec( p_pipe[ 0 ] );
}

static void _test_close( test_internal_s * p_test  )
{
	int close = 0;
	pipe_interface p_pipe_i;
	gc_interface p_gc_i;
	
	pthread_mutex_lock( &p_test->mutex );
	if( p_test->status >= 0 ){
		MOON_PRINT( TEST, NULL, "p_test close:%p", p_test );
		p_test->status = -1;
		close = 1;
	}
	pthread_mutex_unlock( &p_test->mutex );
	if( close == 1 ){
		p_pipe_i = FIND_INTERFACE( p_test->p_pipe, pipe_interface_s );
		p_gc_i = FIND_INTERFACE( p_test->p_pipe, gc_interface_s );
		p_pipe_i->close( p_test->p_pipe );
		p_gc_i->ref_dec( p_test->p_pipe );
		p_test->p_pipe = 0;
		pthread_mutex_destroy( &p_test->mutex );
	}
}

static void test_close( void * p_data, void * p_pipe_data )
{
	_test_close( p_data );
}

static void test_echo( void * p_data, void * p_pipe )
{
	test_internal_s * p_test;
	pipe_test_data_s * p_pipe_data;

	p_test = p_data;
	p_pipe_data = p_pipe;
	pthread_mutex_lock( &p_test->mutex );
	if( p_test->status >= 0 ){
		MOON_PRINT( TEST, NULL, "a:%d", p_pipe_data->a );
	}else{
		MOON_PRINT( TEST, NULL, "already closed:%p", p_test );
	}
	pthread_mutex_unlock( &p_test->mutex );

}
static void _test_func2( common_user_data p_user_data )
{
	test_internal_s * p_test;
	void * p_pipe_data = NULL;
	pipe_interface p_pipe_i;
	gc_interface p_gc_i;
	test_interface_s * p_test_i;
	void * p_point, * p_point_data;
	int ret;

	p_test = p_user_data->ptr;
	pthread_mutex_lock( &p_test->mutex );
	if( p_test->status >= 0 ){
		p_pipe_i = FIND_INTERFACE( p_test->p_pipe, pipe_interface_s );
		p_gc_i = FIND_INTERFACE( p_test->p_pipe, gc_interface_s );
		p_pipe_data = p_test->p_pipe;
		p_gc_i->ref_inc( p_pipe_data );
	}else{
		CALL_INTERFACE_FUNC( p_test, gc_interface_s, ref_dec );
		MOON_PRINT( TEST, NULL, "already closed:%p", p_test );
	}
	pthread_mutex_unlock( &p_test->mutex );
	if( p_pipe_data == NULL ){
		return;
	}
	MOON_PRINT( TEST, NULL, "begain get ref" );
	ret = p_pipe_i->get_other_point_ref( p_pipe_data, &p_point, &p_point_data );
	MOON_PRINT( TEST, NULL, "end get ref:%d", ret );
	if( ret >= 0 ){
		p_test_i = FIND_INTERFACE( p_point, test_interface_s );
		p_test_i->echo( p_point, p_point_data );
	
		CALL_INTERFACE_FUNC( p_point, gc_interface_s, ref_dec );
		CALL_INTERFACE_FUNC( p_point_data, gc_interface_s, ref_dec );
	}
	CALL_INTERFACE_FUNC( p_test, gc_interface_s, ref_dec );
	CALL_INTERFACE_FUNC( p_pipe_data, gc_interface_s, ref_dec );
}

static void test_ref_inc( void * p_data )
{
	GC_REF_INC( ( test_internal_s * )p_data );
}

static void test_ref_dec( void * p_data )
{
	test_internal_s * p_test;
	int ref_num;

	p_test = p_data;
	ref_num = GC_REF_DEC( p_test );
	if( ref_num == 0 ){
		MOON_PRINT( TEST, NULL, "free test" );
		if( p_test->status >=  0 ){
			MOON_PRINT( TEST, NULL, "test not closed when free!" );
		}
		free( GET_INTERFACE_START_POINT( p_test ) );
	}else if( ref_num < 0 ){
		MOON_PRINT( TEST, NULL, "test ref num under overflow!" );
	}
}

static void test_func2()
{
	test_internal_s * p_test[ 2 ];
	pipe_test_data_s * p_data[ 2 ];
	pipe_interface p_pipe_i;
	gc_interface p_gc_i;
	pthreadpool_interface p_pp_i;
	process_task_s task;
	void * p_ppool;
	int i;

	for( i = 0; i < 2; i++ ){
		p_test[ i ] = MALLOC_INTERFACE_ENTITY( sizeof( test_internal_s ), 0, 0 );
		BEGAIN_INTERFACE( p_test[ i ] );
		END_INTERFACE( GET_STATIC_INTERFACE_HANDLE( test_hub ) );
		p_test[ i ]->ref_num = 1;
	}
	p_ppool = get_pthreadpool_instance();
	p_pp_i = FIND_INTERFACE( p_ppool, pthreadpool_interface_s );
	pipe_new( p_data, sizeof( pipe_test_data_s ), sizeof( pipe_test_data_s ), 1 );
	p_pipe_i = FIND_INTERFACE( p_data[ 0 ], pipe_interface_s );
	p_gc_i = FIND_INTERFACE( p_data[ 1 ], gc_interface_s );
	
	p_pipe_i->set_point_ref( p_data[ 0 ], p_test[ 0 ] );
	pthread_mutex_lock( &p_test[ 0 ]->mutex );
	p_data[ 0 ]->a = 0;
	p_test[ 0 ]->p_pipe = p_data[ 0 ];
	pthread_mutex_unlock( &p_test[ 0 ]->mutex );
	//add close
//	p_pipe_i->close( p_data[ 0 ] );
	task.user_data[ 0 ].ptr = p_test[ 0 ];
	task.task_func = _test_func2;
	for( i = 0; i < 30; i++ ){
		MOON_PRINT( TEST, NULL, "put task" );
		test_ref_inc( p_test[ 0 ] );
		p_pp_i->put_task( p_ppool, &task );
	}
	
	p_pipe_i->set_point_ref( p_data[ 1 ], p_test[ 1 ] );
	pthread_mutex_lock( &p_test[ 1 ]->mutex );
	p_data[ 1 ]->a = 1;
	p_gc_i->ref_inc( p_data[ 1 ] );
	p_test[ 1 ]->p_pipe = p_data[ 1 ];
	pthread_mutex_unlock( &p_test[ 1 ]->mutex );
	//add close
//	p_pipe_i->close( p_data[ 0 ] );

	MOON_PRINT( TEST, NULL, "init done" );
	p_pipe_i->init_done( p_data[ 1 ], 0 );
	//add close
	usleep( 1 );
	p_pipe_i->close( p_data[ 0 ] );

	task.user_data[ 0 ].ptr = p_test[ 1 ];
	for( i = 0; i < 30; i++ ){
		MOON_PRINT( TEST, NULL, "put task" );
		test_ref_inc( p_test[ 1 ] );
		p_pp_i->put_task( p_ppool, &task );
	}
//	add close
//	p_pipe_i->close( p_data[ 0 ] );

	sleep( 2 );

	_test_close( p_test[ 0 ] );
	_test_close( p_test[ 1 ] );
	test_ref_dec( p_test[ 0 ] );
	test_ref_dec( p_test[ 1 ] );
}

void main()
{
	test_func2();
}

