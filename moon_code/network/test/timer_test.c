#include<stdio.h>
#include<unistd.h>
#include"moon_timer.h"
#include"moon_pipe.h"
#include"common_interfaces.h"

typedef struct{
	int id;
} test_pipe_data_s;

typedef struct{
	int a;
	test_pipe_data_s * p_timer_pipe;
} test_internal_s;

static void test_timer_func( void * p_data, void * p_pipe, int mark_id );

STATIC_BEGAIN_INTERFACE( test_hub )
STATIC_DECLARE_INTERFACE( timer_listen_interface_s )
STATIC_END_DECLARE_INTERFACE( test_hub, 1, test_internal_s test )
STATIC_GET_INTERFACE( test_hub, timer_listen_interface_s, 0 ) = {
	.times_up = test_timer_func		
}
STATIC_END_INTERFACE( NULL )

static void test_timer_func( void * p_data, void * p_pipe, int mark_id )
{
	test_internal_s * p_test;
	test_pipe_data_s * p_pipe_data;
	void * p_data1, * p_point_data1;
	pipe_interface_s * p_pipe_i;
	timer_desc_s desc;

	p_test = p_data;
	p_pipe_data = p_pipe;
	desc.repeat_num = p_test->a;
	desc.time_us = 1* 100000;
	desc.mark_id = mark_id;
	MOON_PRINT( TEST, NULL, "int timer:%d,%d", p_pipe_data->id, mark_id );
/*	
	p_test->a--;
	if( ( p_test->a % 2 ) ==  0 ){
		p_pipe_i = FIND_INTERFACE( p_pipe, pipe_interface_s );
		p_pipe_i->get_other_point_ref( p_pipe, &p_data1, &p_point_data1 );
	//	CALL_INTERFACE_FUNC( p_data1, timer_interface_s, set, p_point_data1, &desc );
		CALL_INTERFACE_FUNC( p_data1, timer_interface_s, stop, p_point_data1);

	}
*/	
}

void main()
{
	void * p_timer;
	void * p_pipe[ 2 ], * p_tmp;
	test_internal_s * p_test;
	int len;
	timer_interface_s * p_timer_i;
	pipe_listener_interface_s * p_pl_i; 
	pipe_interface_s * p_pipe_i;
	timer_desc_s desc = { 1, 0, 1000000 };
	int i;

	p_test = &test_hub.test;
	p_test->a = 5;
	p_timer = get_timer_instance();
	p_timer_i = FIND_INTERFACE( p_timer, timer_interface_s );
	p_pl_i = FIND_INTERFACE( p_timer, pipe_listener_interface_s );
	
	for( i = 0; i < 2; i++ ){
		p_pl_i->get_pipe_data_len( p_timer, &len );
		pipe_new( p_pipe, sizeof( test_pipe_data_s ), len, 1 );
		p_pipe_i = FIND_INTERFACE( p_pipe[ 0 ], pipe_interface_s );

		p_pipe_i->set_point_ref( p_pipe[ 0 ], p_test );
		//p_test->p_timer_pipe = p_pipe[ 0 ];
		//p_test->p_timer_pipe->id = 1;
		CALL_INTERFACE_FUNC( p_pipe[ 0 ], gc_interface_s, ref_inc );
		MOON_PRINT( TEST, NULL, "put timer" );
		desc.mark_id = i;
		p_timer_i->new_timer( p_timer, p_pipe[ 1 ], &desc );
		if( i == 0 ){
			p_tmp = p_pipe[ 1 ];
		}
	}
	sleep( 3 );
	p_timer_i->stop( p_timer, p_tmp );
//	sleep( 1 );
	
//	p_timer_i->restart( p_timer, p_pipe[ 1 ] );
//	desc.time_us = 4000000;
//	desc.mark_id = 2;
//	p_timer_i->set( p_timer, p_pipe[ 1 ], &desc );
//	sleep( 1 );
//	p_timer_i->del( p_timer, p_pipe[ 1 ] );

	sleep( 10 );
}

