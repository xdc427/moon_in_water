//#include<stdio.h>
#include<stdlib.h>
#include<pthread.h>
#include"moon_pipe.h"
#include"common_interfaces.h"

#ifdef MODENAME
#undef MODENAME
#endif
#define MODENAME "moon_pipe"

#ifdef MOON_TEST
static const char pipe_xid[] = "pipe_test";
#endif

#define GET_PIPE( p_pipe_data ) ( ( moon_pipe )( ( char *)( p_pipe_data ) -\
 ( p_pipe_data )->pipe_offset ) ) 
#define GET_OTHER_POINT( p_pipe_data, p_pipe ) ( ( char *)( p_pipe ) +\
 ( p_pipe_data )->point_offset )

enum{
	PIPE_STATUS_READY = 0x1,
	PIPE_POINT0_CLOSED = 0x2,
	PIPE_POINT1_CLOSED = 0x4,
	PIPE_CLOSED_MASK = PIPE_POINT0_CLOSED | PIPE_POINT1_CLOSED,
	PIPE_STATUS_MASK = 0x7,
	PIPE_USEING_REF_UNIT = 0x8
}timer_status_e;

enum{
	PIPE_NO_MUTEX = 0x1,
	PIPE_MUTEX_CAN_DEL = STATUS_CLOSED
};

//moon_pipe_s + ( pipe_point_data_s + ( pipe_interface data ) + ( user data ) )[ 2 ]
typedef struct moon_pipe_s{
	int ref_num;
	int status;
	int mutex_status;
	pthread_mutex_t pipe_mutex;
} moon_pipe_s;
typedef moon_pipe_s * moon_pipe;

typedef struct pipe_point_data_s{
	int index;//0 or 1
	int pipe_offset;//pipe_point_data offset to moon_pipe_s
	int point_offset;//other p_data offset to moon_pipe_s
	gc_interface p_cache_gc;
	pipe_listener_interface p_cache_listener;
	void ( *free_pipe_data )( void * );
	void * p_ref;
} pipe_point_data_s;
typedef pipe_point_data_s * pipe_point_data;

static int pipe_set_point_ref( void * p_data, void * p_ref );
static void pipe_init_done( void * p_data, int is_fail );
static void pipe_close( void * p_data );
static int pipe_get_other_point_ref( void * p_data, void ** pp_ref, void ** pp_data_ref );
static void pipe_ref_inc( void * p_data );
static void pipe_ref_dec( void * p_data );

STATIC_BEGAIN_INTERFACE( pipe_hub )
STATIC_DECLARE_INTERFACE( gc_interface_s )
STATIC_DECLARE_INTERFACE( pipe_interface_s )
STATIC_END_DECLARE_INTERFACE( pipe_hub, 2 )
STATIC_GET_INTERFACE( pipe_hub, gc_interface_s, 0 ) = {
	.ref_inc = pipe_ref_inc,
	.ref_dec = pipe_ref_dec
}
STATIC_GET_INTERFACE( pipe_hub, pipe_interface_s, 1 ) = {
	.init_done = pipe_init_done,
	.set_point_ref = pipe_set_point_ref,
	.get_other_point_ref = pipe_get_other_point_ref,
	.close = pipe_close
}
STATIC_END_INTERFACE( NULL )

static int pipe_useing_ref_inc( void * p_data )
{
	int tmp;
	moon_pipe p_pipe;
	pipe_point_data p_pipe_data;

	p_pipe_data = ( pipe_point_data )GET_INTERFACE_START_POINT( p_data ) - 1;
	p_pipe = GET_PIPE( p_pipe_data );
	do{
		tmp = p_pipe->status;
	}while( ( ( tmp & PIPE_CLOSED_MASK ) == 0 ) 
			&& !__sync_bool_compare_and_swap( &p_pipe->status, tmp, tmp + PIPE_USEING_REF_UNIT ) );
	if( ( tmp & PIPE_CLOSED_MASK ) != 0 ){
		return -1;
	}
	if( tmp + PIPE_USEING_REF_UNIT < 0 ){
		MOON_PRINT_MAN( ERROR, "useing ref up overflow!" );
	}
	MOON_PRINT( TEST, pipe_xid, "%p:useing:1", p_pipe );
	return 0;
}

static int pipe_useing_ref_dec( void * p_data )
{
	int tmp;
	pipe_point_data p_pipe_data0, p_pipe_data1;
	moon_pipe p_pipe;

	p_pipe_data0 = ( pipe_point_data )GET_INTERFACE_START_POINT( p_data ) - 1;
	p_pipe = GET_PIPE( p_pipe_data0 );
	p_pipe_data1 = ( pipe_point_data )GET_INTERFACE_START_POINT( 
		GET_OTHER_POINT( p_pipe_data0, p_pipe ) ) - 1;
	MOON_PRINT( TEST, pipe_xid, "%p:useing:-1", p_pipe );
	tmp = __sync_sub_and_fetch( &p_pipe->status, PIPE_USEING_REF_UNIT );
	if( tmp < 0 ){
		MOON_PRINT_MAN( ERROR, "useing ref dowm overflow" );
	}else if(  tmp < PIPE_USEING_REF_UNIT && ( tmp & PIPE_CLOSED_MASK ) != 0  ){
		if( ( tmp & PIPE_CLOSED_MASK ) == ( PIPE_POINT0_CLOSED << p_pipe_data0->index ) ){
			CALL_INTERFACE_HANDLE_FUNC( p_pipe_data1->p_cache_listener, close
				, p_pipe_data1->p_ref, GET_OTHER_POINT( p_pipe_data0, p_pipe ) );
		}else if( ( tmp & PIPE_CLOSED_MASK ) == ( PIPE_POINT0_CLOSED << p_pipe_data1->index ) ){
			CALL_INTERFACE_HANDLE_FUNC( p_pipe_data0->p_cache_listener, close
				, p_pipe_data0->p_ref, p_data );
		}
		CALL_INTERFACE_HANDLE_FUNC( p_pipe_data0->p_cache_gc, ref_dec, p_pipe_data0->p_ref );
		CALL_INTERFACE_HANDLE_FUNC( p_pipe_data1->p_cache_gc, ref_dec, p_pipe_data1->p_ref );
		MOON_PRINT( TEST, pipe_xid, "%p:pipe_closed:1", p_pipe );
	}
	return 0;
}

int pipe_new( void ** ptr, int len1, int len2, int is_two_way )
{
	int i_len, i;
	moon_pipe p_pipe;
	pipe_point_data p_pipe_data[ 2 ];
	void * p_data[ 2 ];

	i_len = CACULATE_INTERFACE_ENTITY_LEN( 0, 0 );
	p_pipe = ( moon_pipe )calloc( sizeof( moon_pipe_s ) 
		+ ( sizeof( pipe_point_data_s ) + i_len ) * 2 + len1 + len2, 1 );
	if( p_pipe == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		return -1;
	}
	p_pipe->ref_num = 1;
	if( is_two_way == 0 ){
		p_pipe->status = PIPE_STATUS_READY;
		p_pipe->mutex_status = PIPE_NO_MUTEX;
	}else if( pthread_mutex_init( &p_pipe->pipe_mutex, NULL ) != 0 ){
		MOON_PRINT_MAN( ERROR, "pipe mutex init error!" );
		free( p_pipe );
		return -1;
	}

	p_pipe_data[ 0 ] = ( pipe_point_data )( p_pipe + 1 );
	p_pipe_data[ 1 ] = ( pipe_point_data )( ( char * )( p_pipe_data[ 0 ] + 1 ) + i_len + len1 );
	p_data[ 0 ] = ( char * )( p_pipe_data[ 0 ] + 1 ) + i_len;
	p_data[ 1 ] = ( char * )( p_pipe_data[ 1 ] + 1 ) + i_len;
	for( i = 0; i < 2; i++ ){
		INIT_INTERFACE_ENTITY( p_pipe_data[ i ] + 1, p_data[ i ] );
		BEGAIN_INTERFACE( p_data[ i ] );
		END_INTERFACE( GET_STATIC_INTERFACE_HANDLE( pipe_hub ) );
		p_pipe_data[ i ]->index = i;
		p_pipe_data[ i ]->pipe_offset = POINT_OFFSET( p_pipe_data[ i ], p_pipe );
		p_pipe_data[ i ]->point_offset = POINT_OFFSET( p_data[ ( i + 1 ) % 2 ], p_pipe );
	}
	ptr[ 0 ] = p_data[ 0 ];
	ptr[ 1 ] = p_data[ 1 ];
	pipe_useing_ref_inc( p_data[ 0 ] );
	if( ( p_pipe->mutex_status & PIPE_NO_MUTEX ) != PIPE_NO_MUTEX ){
		pthread_mutex_lock( &p_pipe->pipe_mutex );
	}
	MOON_PRINT( TEST, pipe_xid, "%p:pipe_new:1", p_pipe );
	return 0;
}

static void pipe_close( void * p_data )
{
	int tmp;
	pipe_point_data p_pipe_data0, p_pipe_data1;
	moon_pipe p_pipe;

	p_pipe_data0 = ( pipe_point_data )GET_INTERFACE_START_POINT( p_data ) - 1;
	p_pipe = GET_PIPE( p_pipe_data0 );
	p_pipe_data1 = ( pipe_point_data )GET_INTERFACE_START_POINT( 
		GET_OTHER_POINT( p_pipe_data0, p_pipe ) ) - 1;
	do{
		tmp = p_pipe->status;
	}while( ( tmp & PIPE_CLOSED_MASK ) == 0 
			&& !__sync_bool_compare_and_swap( &p_pipe->status, tmp
				, tmp | ( PIPE_POINT0_CLOSED << p_pipe_data0->index ) ) );
	if( tmp < PIPE_USEING_REF_UNIT && ( tmp & PIPE_CLOSED_MASK ) == 0 ){
		MOON_PRINT( TEST, pipe_xid, "%p:pipe_closed:1", p_pipe );
		CALL_INTERFACE_HANDLE_FUNC( p_pipe_data1->p_cache_listener, close
			, p_pipe_data1->p_ref, GET_OTHER_POINT( p_pipe_data0, p_pipe ) );
		CALL_INTERFACE_HANDLE_FUNC( p_pipe_data0->p_cache_gc, ref_dec, p_pipe_data0->p_ref );
		CALL_INTERFACE_HANDLE_FUNC( p_pipe_data1->p_cache_gc, ref_dec, p_pipe_data1->p_ref );
	}
}

//由此函数增加引用计数
//此函数应该在useing的保护下调用
static int pipe_set_point_ref( void * p_data, void * p_ref )
{
	pipe_point_data p_pipe_data;

	p_pipe_data = ( pipe_point_data )GET_INTERFACE_START_POINT( p_data ) - 1;
	p_pipe_data->p_cache_gc = FIND_INTERFACE( p_ref, gc_interface_s );
	p_pipe_data->p_cache_listener = FIND_INTERFACE( p_ref, pipe_listener_interface_s );
	p_pipe_data->free_pipe_data = p_pipe_data->p_cache_listener->free_pipe_data;
	CALL_INTERFACE_HANDLE_FUNC( p_pipe_data->p_cache_gc, ref_inc, p_ref );
	p_pipe_data->p_ref = p_ref;
	return 0;
}

static void mutex_del_func( void * p_data )
{
	moon_pipe p_pipe;

	p_pipe = ( moon_pipe )p_data;
	pthread_mutex_destroy( &p_pipe->pipe_mutex );
	MOON_PRINT( TEST, pipe_xid, "%p:del_mutex:1", p_pipe );
}

static void pipe_init_done( void * p_data, int is_fail )
{
	int tmp;
	moon_pipe p_pipe;
	pipe_point_data p_pipe_data;

	p_pipe_data = ( pipe_point_data )GET_INTERFACE_START_POINT( p_data ) - 1;
	p_pipe = GET_PIPE( p_pipe_data );
	MOON_PRINT( TEST, pipe_xid, "%p:pipe_init_done:1", p_pipe );
	if( is_fail != 0 ){
		pipe_close( p_data );
	}else if( ( p_pipe->status & PIPE_STATUS_READY ) == 0 ){
		do{
			tmp = p_pipe->status;
		}while( !__sync_bool_compare_and_swap( &p_pipe->status, tmp, tmp | PIPE_STATUS_READY ) );
	}
	if( ( p_pipe->mutex_status & PIPE_NO_MUTEX ) != PIPE_NO_MUTEX ){
		pthread_mutex_unlock( &p_pipe->pipe_mutex );
		set_status_closed( &p_pipe->mutex_status, mutex_del_func, p_pipe );		
	}
	pipe_useing_ref_dec( p_data );
}

//error: -1, >= 0 ok
static int pipe_get_other_point_ref( void * p_data, void ** pp_ref, void ** pp_data_ref )
{
	pipe_point_data p_pipe_data0, p_pipe_data1;
	moon_pipe p_pipe;

	p_pipe_data0 = ( pipe_point_data )GET_INTERFACE_START_POINT( p_data ) - 1;
	p_pipe = GET_PIPE( p_pipe_data0 );
	p_pipe_data1 = ( pipe_point_data )GET_INTERFACE_START_POINT( 
		GET_OTHER_POINT( p_pipe_data0, p_pipe ) ) - 1;

	if( pipe_useing_ref_inc( p_data ) < 0 ){
		return -1;
	}
	if( ( p_pipe->status & PIPE_STATUS_READY ) == 0 
		&& useing_ref_inc( &p_pipe->mutex_status ) >= 0 ){
		MOON_PRINT( TEST, pipe_xid, "%p:mutex_ref:1", p_pipe );
		pthread_mutex_lock( &p_pipe->pipe_mutex );
		pthread_mutex_unlock( &p_pipe->pipe_mutex );
		MOON_PRINT( TEST, pipe_xid, "%p:mutex_ref:-1", p_pipe );
		useing_ref_dec( &p_pipe->mutex_status, mutex_del_func, p_pipe );
	}
	if( ( p_pipe->status & PIPE_STATUS_READY ) == 0 
		|| ( p_pipe->status & PIPE_CLOSED_MASK ) != 0 ){
		pipe_useing_ref_dec( p_data );
		return -1;
	}
	MOON_PRINT( TEST, pipe_xid, "%p:pipe_get_ref:1", p_pipe );
	CALL_INTERFACE_HANDLE_FUNC( p_pipe_data1->p_cache_gc, ref_inc, p_pipe_data1->p_ref );
	*pp_ref = p_pipe_data1->p_ref;
	pipe_ref_inc( p_data );
	*pp_data_ref = GET_OTHER_POINT( p_pipe_data0, p_pipe );
	pipe_useing_ref_dec( p_data );
	return 0;
}

static void pipe_ref_inc( void * p_data )
{
	pipe_point_data p_pipe_data0;
	moon_pipe p_pipe;

	p_pipe_data0 = ( pipe_point_data )GET_INTERFACE_START_POINT( p_data ) - 1;
	p_pipe = GET_PIPE( p_pipe_data0 );
	GC_REF_INC( p_pipe );
	MOON_PRINT( TEST, pipe_xid, "%p:pipe_ref:1", p_pipe );
}

static void pipe_ref_dec( void * p_data )
{
	pipe_point_data p_pipe_data0, p_pipe_data1;
	moon_pipe p_pipe;
	int ref_num;

	p_pipe_data0 = ( pipe_point_data )GET_INTERFACE_START_POINT( p_data ) - 1;
	p_pipe = GET_PIPE( p_pipe_data0 );
	p_pipe_data1 = ( pipe_point_data )GET_INTERFACE_START_POINT( 
		GET_OTHER_POINT( p_pipe_data0, p_pipe ) ) - 1;

	MOON_PRINT( TEST, pipe_xid, "%p:pipe_ref:-1", p_pipe );
	ref_num = GC_REF_DEC( p_pipe );
	if( ref_num == 0 ){
		MOON_PRINT( TEST, pipe_xid, "%p:pipe_free:1", p_pipe );
		if( ( p_pipe->status & PIPE_CLOSED_MASK ) == 0 ){
			MOON_PRINT_MAN( ERROR, "pipe not closed when free!" );
		}
		CALL_FUNC( p_pipe_data0->free_pipe_data, p_data );
		CALL_FUNC( p_pipe_data1->free_pipe_data, GET_OTHER_POINT( p_pipe_data0, p_pipe ) );
		free( p_pipe );
	}else if( ref_num < 0 ){
		MOON_PRINT_MAN( ERROR, "ref num under overflow!" );
	}
}

