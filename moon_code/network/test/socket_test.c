#include<stdio.h>
#include<sys/time.h>
#include<unistd.h>
#include<stdlib.h>
#include<pthread.h>
#include"moon_pipe.h"
#include"common_socket.h"
#include"common_interfaces.h"
#include"moon_pthread_pool.h"

#ifdef MODENAME
#undef MODENAME
#endif
#define MODENAME "common_socket_test"

static const char xid[] = "echo";
static int socket_id = 0;
const char base[] = "abcdefghjkmnpqrstwxyz23456789";

enum{
	TEST_CLOSED = 0x1,
	TEST_CAN_READ = 0x2,
	TEST_CAN_WRITE = 0x4,
	TEST_ECHO = 0x8,
	TEST_READING = 0x10,
	TEST_WRITING = 0x20
};

typedef struct{
	int status;
	int is_accept;
	int socket_id;
	//client
	int total_len;
	int left_len;
	int already_len;
	//
	pthread_mutex_t mutex;
	char buf[ 1024 ];
	int offset;
	int len;
} socket_type_s;
typedef socket_type_s * socket_type;

static void get_pipe_data_len( void *, int * p_len );
static int socket_ready( void * p_data, void * p_pipe, void * p_new_pipe );
static int test_recv( void * p_data, void * p_pipe );
static int test_send( void * p_data, void * p_pipe );
static void test_close( void * p_data, void * p_pipe );
static void free_pipe_data( void * p_pipe );

STATIC_BEGAIN_INTERFACE( order_hub )
STATIC_DECLARE_INTERFACE( order_listener_interface_s )
STATIC_DECLARE_INTERFACE( pipe_listener_interface_s )
STATIC_END_DECLARE_INTERFACE( order_hub, 2 )
STATIC_GET_INTERFACE( order_hub, order_listener_interface_s, 0 ) = {
	.on_ready = socket_ready
}
STATIC_GET_INTERFACE( order_hub, pipe_listener_interface_s, 1 ) = {
	.close = test_close,
	.get_pipe_data_len = get_pipe_data_len
}
STATIC_END_INTERFACE( NULL )

STATIC_BEGAIN_INTERFACE( iolistener_hub )
STATIC_DECLARE_INTERFACE( io_listener_interface_s )
STATIC_DECLARE_INTERFACE( pipe_listener_interface_s )
STATIC_END_DECLARE_INTERFACE( iolistener_hub, 2 )
STATIC_GET_INTERFACE( iolistener_hub, io_listener_interface_s, 0 ) = {
	.recv_event = test_recv,
	.send_event = test_send,
	.close_event = test_close
}
STATIC_GET_INTERFACE( iolistener_hub, pipe_listener_interface_s, 1 ) = {
	.close = test_close,
	.free_pipe_data = free_pipe_data
}
STATIC_END_INTERFACE( NULL )

static void get_pipe_data_len( void *p_data, int * p_len )
{
	*p_len = sizeof( socket_type_s );
}

static void _test_close( socket_type p_type )
{
	int ret, is_notclose = 0;
	void * p_point, * p_point_data;
	pipe_interface p_pipe_i;

	pthread_mutex_lock( &p_type->mutex );
	is_notclose = p_type->status & TEST_CLOSED;
	p_type->status |= TEST_CLOSED;
	pthread_mutex_unlock( &p_type->mutex );
	if( is_notclose == 0 ){
		MOON_PRINT( TEST, xid, "test_close:%d:%d", p_type->socket_id, p_type->is_accept );
		p_pipe_i = FIND_INTERFACE( p_type, pipe_interface_s );	
		ret = p_pipe_i->get_other_point_ref( p_type, &p_point, &p_point_data );
		if( ret >= 0 ){
			CALL_INTERFACE_FUNC( p_point, io_pipe_interface_s, close, p_point_data );
			CALL_INTERFACE_FUNC( p_point, gc_interface_s, ref_dec );
			CALL_INTERFACE_FUNC( p_point_data, gc_interface_s, ref_dec );
		}
		CALL_INTERFACE_FUNC( p_type, pipe_interface_s, close );
		CALL_INTERFACE_FUNC( p_type, gc_interface_s, ref_dec );
	}
}

static int socket_ready( void * p_data, void * p_pipe, void * p_new_pipe )
{
	socket_type p_type, p_new_type;
	pipe_interface p_pipe_i;
	void * p_point, * p_point_data;
	int ret;

	MOON_PRINT( TEST, NULL, "onready_pfm_before" );
	p_type = p_pipe;
	p_new_type = p_new_pipe;
	p_new_type->is_accept = p_type->is_accept;
	p_new_type->socket_id = p_type->socket_id;
	if( p_new_type->is_accept ){
		p_new_type->socket_id = __sync_fetch_and_add( &socket_id, 1 );
	}
	if( ( p_new_type->socket_id % 100 ) == 0 ){
		p_new_type->left_len = ( rand() % 512 * 1024 ) + 1024 * 1024;
	}else if( ( p_new_type->socket_id % 50 ) == 0 ){
		p_new_type->left_len = ( rand() % 100 * 1024 ) + 100 * 1024;
	}else{
		p_new_type->left_len = ( rand() % 1024 ) + 1;
	}
	p_new_type->total_len = p_new_type->left_len;
	pthread_mutex_init( &p_new_type->mutex, NULL );
	p_pipe_i = FIND_INTERFACE( p_new_pipe, pipe_interface_s );
	p_pipe_i->set_point_ref( p_new_pipe, &iolistener_hub + 1 );
	CALL_INTERFACE_FUNC( p_new_pipe, gc_interface_s, ref_inc );
	p_pipe_i->init_done( p_new_pipe, 0 );
	
	MOON_PRINT( TEST, NULL, "on_ready:%d:%d", p_new_type->is_accept, p_new_type->socket_id );
	ret = p_pipe_i->get_other_point_ref( p_new_pipe, &p_point, &p_point_data );
	if( ret >= 0 ){
		CALL_INTERFACE_FUNC( p_point, io_pipe_interface_s, control, p_point_data, SOCKET_START );
		CALL_INTERFACE_FUNC( p_point, gc_interface_s, ref_dec );
		CALL_INTERFACE_FUNC( p_point_data, gc_interface_s, ref_dec );
	}else{
		_test_close( p_new_type );
	}
	MOON_PRINT( TEST, NULL, "onready_pfm_end" );
	return 0;
}

static int is_can_echo( int * p_status )
{
	int is_can = 0;

	if( ( *p_status & ( TEST_ECHO | TEST_CAN_READ | TEST_CAN_WRITE ) )
			==  ( TEST_CAN_READ | TEST_CAN_WRITE ) ){
		*p_status |= TEST_ECHO;
		*p_status &= ~( TEST_CAN_READ | TEST_CAN_WRITE );
		is_can = 1;
	}
	return is_can;
}

static int is_can_read( int * p_status )
{
	int is_can = 0;

	is_can = *p_status & TEST_READING;
	is_can /= TEST_READING;
	is_can ^= 1;
	*p_status &= ~( TEST_CAN_READ * is_can );
	*p_status |= TEST_READING * is_can;

	return is_can;
}

static int is_can_write( int * p_status )
{
	int is_can = 0;

	is_can = *p_status & TEST_WRITING;
	is_can /= TEST_WRITING;
	is_can ^= 1;
	*p_status &= ~( TEST_CAN_WRITE * is_can );
	*p_status |= TEST_WRITING * is_can;
	
	return is_can;
}

static int echo_task( common_user_data p_user_data )
{
	io_pipe_interface p_io_i;
	socket_type p_type;
	int ret, again;

	p_type = p_user_data[ 0 ].ptr;
	p_io_i = FIND_INTERFACE( p_user_data[ 1 ].ptr, io_pipe_interface_s );
	while( ( p_type->status & TEST_CLOSED ) == 0 ){
		if( p_type->len == 0 ){
			ret = p_io_i->read( p_user_data[ 1 ].ptr, p_user_data[ 2 ].ptr
				, p_type->buf, sizeof( p_type->buf ) - 1, 0 );
			if( ret > 0 ){
				p_type->offset = 0;
				p_type->len = ret;
			}else{
				pthread_mutex_lock( &p_type->mutex );
				again = p_type->status & TEST_CAN_READ;
				again /= TEST_CAN_READ;
				p_type->status &= ~( TEST_CAN_READ | TEST_ECHO );
				p_type->status |= TEST_ECHO * again;
				p_type->status |= TEST_CAN_WRITE * ( again ^ 1 );
				pthread_mutex_unlock( &p_type->mutex );
				if( again ){
					continue;
				}
				break;
			}
		}
		ret = p_io_i->write( p_user_data[ 1 ].ptr, p_user_data[ 2 ].ptr
				, p_type->buf + p_type->offset, p_type->len, 0 );
		if( ret > 0 ){
			p_type->offset += ret;
			p_type->len -= ret;
		}else{
			pthread_mutex_lock( &p_type->mutex );
			again = p_type->status & TEST_CAN_WRITE;
			again /= TEST_CAN_WRITE;
			p_type->status &= ~( TEST_CAN_WRITE | TEST_ECHO );
			p_type->status |= TEST_ECHO * again;
			p_type->status |= TEST_CAN_READ * ( again ^ 1);
			pthread_mutex_unlock( &p_type->mutex );
			if( again ){
				continue;
			}
			break;
		}
	}
	CALL_INTERFACE_FUNC( p_type, gc_interface_s, ref_dec );
	CALL_INTERFACE_FUNC( p_user_data[ 1 ].ptr, gc_interface_s, ref_dec );
	CALL_INTERFACE_FUNC( p_user_data[ 2 ].ptr, gc_interface_s, ref_dec );
	return 0;
}

static int client_read( common_user_data p_user_data )
{
	io_pipe_interface p_io_i;
	socket_type p_type;
	int ret, again;

	p_type = p_user_data[ 0 ].ptr;
	p_io_i = FIND_INTERFACE( p_user_data[ 1 ].ptr, io_pipe_interface_s );
	while( ( p_type->status & TEST_CLOSED ) == 0 ){
		ret = p_io_i->read( p_user_data[ 1 ].ptr, p_user_data[ 2 ].ptr
				, p_type->buf, sizeof( p_type->buf ) - 1, 0 );
		if( ret > 0 ){
			p_type->already_len += ret;
			p_type->buf[ ret ] = '\0';
			MOON_PRINT( TEST, xid, "%d:recv:%s", p_type->socket_id, p_type->buf );
			if( p_type->already_len == p_type->total_len ){
				_test_close( p_type );
				break;
			}
		}else{
			pthread_mutex_lock( &p_type->mutex );
			again = p_type->status & TEST_CAN_READ;
			again /= TEST_CAN_READ;
			p_type->status &= ~( TEST_CAN_READ | TEST_READING );
			p_type->status |= TEST_READING * again;
			pthread_mutex_unlock( &p_type->mutex );
			if( again ){
				continue;
			}
			break;
		}
	}
	CALL_INTERFACE_FUNC( p_type, gc_interface_s, ref_dec );
	CALL_INTERFACE_FUNC( p_user_data[ 1 ].ptr, gc_interface_s, ref_dec );
	CALL_INTERFACE_FUNC( p_user_data[ 2 ].ptr, gc_interface_s, ref_dec );
	return 0;
}

static int client_write( common_user_data p_user_data )
{
	io_pipe_interface p_io_i;
	socket_type p_type;
	int ret, again, len, j;
	char buf[ 1024 ];

	p_type = p_user_data[ 0 ].ptr;
	p_io_i = FIND_INTERFACE( p_user_data[ 1 ].ptr, io_pipe_interface_s );
	while( ( p_type->status & TEST_CLOSED ) == 0 ){
		len = MIN( sizeof( buf ) - 1, p_type->left_len );
		for( j = 0; j < len; j++ ){
			buf[ j ] = base[ rand() % ( sizeof( base ) - 1 ) ];
		}
		ret = p_io_i->write( p_user_data[ 1 ].ptr, p_user_data[ 2 ].ptr
				, buf, len, 0 );
		if( ret > 0 ){
			p_type->left_len -= ret;
			buf[ ret ] = '\0';
			MOON_PRINT( TEST, xid, "%d:send:%s", p_type->socket_id, buf );
			if( p_type->left_len == 0 ){
				break;
			}
		}else{
			pthread_mutex_lock( &p_type->mutex );
			again = p_type->status & TEST_CAN_WRITE;
			again /= TEST_CAN_WRITE;
			p_type->status &= ~( TEST_CAN_WRITE | TEST_WRITING );
			p_type->status |= TEST_WRITING * again;
			pthread_mutex_unlock( &p_type->mutex );
			if( again ){
				continue;
			}
			break;
		}
	}
	CALL_INTERFACE_FUNC( p_type, gc_interface_s, ref_dec );
	CALL_INTERFACE_FUNC( p_user_data[ 1 ].ptr, gc_interface_s, ref_dec );
	CALL_INTERFACE_FUNC( p_user_data[ 2 ].ptr, gc_interface_s, ref_dec );
	return 0;
}

static int _test_rs( socket_type p_type, int r_or_s )
{
	pipe_interface p_pipe_i;
	void * p_point, * p_point_data;
	int ret;
	process_task_s task;
	void * p_ppool;
	pthreadpool_interface p_ppool_i;

	task.task_func = NULL;
	MOON_PRINT( TEST, NULL, "findi_pfm_before" );
	p_pipe_i = FIND_INTERFACE( p_type, pipe_interface_s );
	MOON_PRINT( TEST, NULL, "findi_pfm_before" );
	MOON_PRINT( TEST, NULL, "testrs_pfm_before" );
	pthread_mutex_lock( &p_type->mutex );
	if( ( p_type->status & TEST_CLOSED ) == 0 ){
		ret = p_pipe_i->get_other_point_ref( p_type, &p_point, &p_point_data );
		if( ret >= 0 ){
			p_type->status |= r_or_s;
			if( p_type->is_accept ){//echo
				ret = is_can_echo( &p_type->status );
				if( ret ){
					task.task_func = echo_task;
				}
			}else if( r_or_s == TEST_CAN_READ ){
				ret = is_can_read( &p_type->status );
				if( ret ){
					task.task_func = client_read;
				}
			}else{
				ret = is_can_write( &p_type->status );
				if( ret && p_type->left_len > 0 ){
					task.task_func = client_write;
				}
			}
			if( task.task_func == NULL ){
				CALL_INTERFACE_FUNC( p_point, gc_interface_s, ref_dec );
				CALL_INTERFACE_FUNC( p_point_data, gc_interface_s, ref_dec );
			}
		}
	}
	pthread_mutex_unlock( &p_type->mutex );
	MOON_PRINT( TEST, NULL, "testrs_pfm_end" );
	if( task.task_func != NULL ){
		p_ppool = get_pthreadpool_instance();
		p_ppool_i = FIND_INTERFACE( p_ppool, pthreadpool_interface_s );
		CALL_INTERFACE_FUNC( p_type, gc_interface_s, ref_inc );
		task.user_data[ 0 ].ptr = p_type;
		task.user_data[ 1 ].ptr = p_point;
		task.user_data[ 2 ].ptr = p_point_data;
		MOON_PRINT( TEST, NULL, "puttask_pfm_before" );
		ret = p_ppool_i->put_task( p_ppool, &task );
		MOON_PRINT( TEST, NULL, "puttask_pfm_end" );
		if( ret < 0 ){
			MOON_PRINT_MAN( ERROR, "put task error" );
			CALL_INTERFACE_FUNC( p_type, gc_interface_s, ref_dec );
			CALL_INTERFACE_FUNC( p_point, gc_interface_s, ref_dec );
			CALL_INTERFACE_FUNC( p_point_data, gc_interface_s, ref_dec );
		}
	}
	return 0;
}

static int test_recv( void * p_data, void * p_pipe )
{
	return _test_rs( p_pipe, TEST_CAN_READ );
}

static int test_send( void * p_data, void * p_pipe )
{
	return _test_rs( p_pipe, TEST_CAN_WRITE );
}

static void test_close( void * p_data, void * p_pipe )
{
	_test_close( p_pipe );
}

static void free_pipe_data( void * p_pipe )
{
	pthread_mutex_destroy( &( ( socket_type )p_pipe )->mutex );
}

void main()
{
	void * p_spool;
	socketpool_interface p_spool_i;
	int len, ret;
	struct timeval tv;
	void * p_pipe[ 2 ];
	socket_type p_type;
	int i;

	len = 0;
	gettimeofday( &tv, NULL );
	srand( tv.tv_usec );
	p_spool = get_socketpool_instance();
	p_spool_i = FIND_INTERFACE( p_spool, socketpool_interface_s );
	CALL_INTERFACE_FUNC( p_spool, pipe_listener_interface_s, get_pipe_data_len, &len );
	
	//sleep( 30 );
	ret = pipe_new( p_pipe, sizeof( socket_type_s ), len, 1 );
	p_type = p_pipe[ 0 ];
	p_type->is_accept = 1;
	pthread_mutex_init( &p_type->mutex, NULL );
	CALL_INTERFACE_FUNC( p_type, pipe_interface_s, set_point_ref, &order_hub + 1 );
	p_spool_i->new_listen_socket( p_spool, p_pipe[ 1 ], "12345" );

	for( i = 0; i < 5000; i++ ){
		ret = pipe_new( p_pipe, sizeof( socket_type_s ), len, 1 );
		if( ret >= 0 ){
			p_type = p_pipe[ 0 ];
			p_type->is_accept = 0;
			p_type->socket_id = __sync_fetch_and_add( &socket_id, 1 );
			pthread_mutex_init( &p_type->mutex, NULL );
			CALL_INTERFACE_FUNC( p_type, pipe_interface_s, set_point_ref, &order_hub + 1 );
			p_spool_i->new_socket( p_spool, p_pipe[ 1 ], "127.0.0.1", "12345" );
			MOON_PRINT( TEST, NULL, "connect_socket:%d", p_type->socket_id );
		}else{
			MOON_PRINT( TEST, NULL, "client_new_error" );
		}
	}
	sleep( 150 );
///	while( 1 );
}

