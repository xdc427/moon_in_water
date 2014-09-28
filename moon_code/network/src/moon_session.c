#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<stdint.h>
#include<time.h>
#include<sys/types.h>
#include<endian.h>
#include"moon_max_min_heap.h"
#include"common_socket.h"
#include"moon_pthread_pool.h"
#include"moon_timer.h"
#include"moon_pipe.h"
#include"common_interfaces.h"
#include"moon_session.h"

/*遵循下面三点，不会有错：
 *1.优先使用管道通讯。
 *2.先改变自身状态再向外发消息。
 *3.初始化时，你把一个东西置于可查寻区域，而自己还要使用时一定要有引用计数。
 */
enum{
	PIPE_STATUS_ACTIVE,
	PIPE_STATUS_STOP,
	PIPE_STATUS_DEL
};

enum{
	SESSION_UNINIT = 0x0,
//for externed
	SESSION_INITED = 0x10
};

enum{
	SESSION_GOAWAY_RECV = 0x1,
	SESSION_GOAWAY_SEND = 0x2,
	SESSION_RECVING = 0x4,
	SESSION_SENDING = 0x8,
	SESSION_CAN_RECV = 0x10,
	SESSION_CAN_SEND = 0x20,
	SESSION_USEING_UNIT = 0x100
};

typedef enum{
	STREAM_INITIAL,
	STREAM_SYN_RECV,
	STREAM_SYN_SEND,
	STREAM_SYNREPLY_RS,
	STREAM_RST_RECV,
	STREAM_SHUT_RECV,//no recved packet
	STREAM_RST_SEND,
	STREAM_CLOSED
} stream_status_e;

typedef enum{
	STREAM_CAN_RECV = 0x1,
	STREAM_RECVING = 0x2,
	STREAM_CAN_SEND = 0x4,
	STREAM_SENDING = 0x8
} stream_shut_flag;

//延迟打包头部，这样可以在头部取各种信息
typedef struct{
	uint64_t pri_seq;//PRI_BIT_LEN:SEQ_BIT_LEN
	stream p_stream;
} output_packet_s;
typedef output_packet_s * output_packet;

#define IS_STREAM_CLOSED( p_stream ) ( p_stream->status == STREAM_CLOSED )
#define CAN_STREAM_ADD_BUF( p_stream ) ( p_stream->status <= STREAM_SYNREPLY_RS )
#define CAN_STREAM_SET_RECV( p_stream ) ( p_stream->status <= STREAM_RST_RECV )
#define CAN_STREAM_SET_SEND( p_stream ) ( p_stream->status <= STREAM_SYNREPLY_RS )
#define CAN_STREAM_ASSOC( p_stream ) ( p_stream->status <= STREAM_SYNREPLY_RS )
//session pipe categories
enum{
	STREAM_RECV_TIMER = 0,
	STREAM_SEND_TIMER,
	STREAM_RST_TIMER,
	STREAM_TIMER_NUM,
	STREAM_CONTROL = STREAM_TIMER_NUM,
	STREAM_SESSION,
	STREAM_ROOT,
	STREAM_PUSHED,
	STREAM_OP,
	STREAM_WIN_OP,
	STREAM_RST_OP,
	STREAM_OP_NUM = STREAM_RST_OP - STREAM_OP + 1,
	STREAM_PIPE_NUM
};

enum{
	STREAM_PIPE_LEN = 2 * sizeof( common_user_data_u ) + sizeof( double_list_s ),
	STREAM_RST_LEN = sizeof( rs_block_s ) + FRAME_RST_LEN,
	STREAM_WIN_UPDATE_LEN = sizeof( rs_block_s ) + FRAME_WINDOW_UPDATE_LEN
};

typedef struct stream_s{
	int ref_num;
	int offset;
	int stream_id;
	int pri;
	int is_pushed;
	int status;
	int shut_flags;
	int send_window_size;
	int send_buf_limit;
	int send_buf_size;
	int recv_window_size;
	int recv_window_base;
	int recv_window_offset;
	int pushed_stream_limit;
	int pushed_stream_num;
	uint64_t mem_used;
	//rs_block_s + FRAME_WINDOW_UPDATE_LEN
	rs_block p_window_update;
	//rs_block_s + FRAME_RST_LEN
	rs_block p_rst;
	rs_buf_hub_s send_hub;
	rs_buf_hub_s recv_hub;
	//pipes
	common_user_data p_timers[ STREAM_TIMER_NUM ];
	common_user_data p_session_pipe;
	common_user_data p_ops[ STREAM_OP_NUM ];
	common_user_data p_control;
	double_list_s pushed_streams;
	void * p_pushed_head;
	pthread_mutex_t deferred_mutex;
} stream_s;
typedef stream_s * stream;

typedef struct{
	stream p_stream;
	rs_block p_block;
	char * buf;
	int cur_buf_index;
	int buf_len;
	int is_drop;//包过大则丢弃。
	char headbuf[ FRAME_HEAD_LEN ];
} active_packet_s;
typedef active_packet_s * active_packet;

//session pipe categories
enum{
	SESSION_RECV_TIMER = 0,
	SESSION_SEND_TIMER,
	SESSION_TIMER_NUM,
	SESSION_CONTROL = SESSION_TIMER_NUM,
	SESSION_PING,
	SESSION_IO,
	SESSION_NEW_STREAM,
	SESSION_LINK_STREAM,
	SESSION_OP_HUB,
	SESSION_OP,
	SESSION_PIPE_NUM
};

enum{
	SESSION_PIPE_LEN = sizeof( common_user_data_u ) + sizeof(  double_list_s ),
	SESSION_AVL_LEN = sizeof( common_user_data_u ) + sizeof( avl_tree_s ) + sizeof( common_user_data_u ),
	SESSION_TIMER_LEN = sizeof( common_user_data_u ) * 2,
	SESSION_OP_LEN = sizeof( common_user_data_u ) * 2,
	SESSION_MAX_RST_MUN = 30,
	SESSION_RST_LEN = sizeof( rs_block_s ) +  FRAME_RST_LEN * SESSION_MAX_RST_MUN + FRAME_PING_LEN * 2,	
	RS_RETRY_MS = 100,
	SEQ_BIT_LEN = 64 - PRI_BIT_LEN
};

#define IS_SESSION_CLOSED( p_session )\
	( ( p_session->status & SESSION_GOAWAY_RECV ) == SESSION_GOAWAY_RECV )
#define IS_SESSION_FREED( p_session )\
	( IS_STREAM_CLOSED( p_session ) && p_session->status < SESSION_USEING_UNIT )
typedef struct{
	int ref_num;
	int is_server;
	int init_status;
	int status;
	int offset;//offset to rs_buf_s
	//streams
	avl_tree p_streams_avl;
	//output packet
	avl_tree p_ops_avl;
	//输入未满一帧时的临时数据
	active_packet_s aip;
	//输出未满一帧时的临时数据
	active_packet_s aop;
	uint64_t next_seq;
	int packet_num;//包含aop中的
	int num_outgoing_streams;
	int num_incoming_streams;
	int next_stream_id;
	int last_recv_stream_id;
	unsigned last_ping_unique_id;
	//( rs_block_s + FRAME_RST_LEN * SESSION_MAX_RST_MUN  )[ 2 ]
	int rst_index;
	int rst_num;
	char * next_buf;
	char * send_ping_buf;
	char * recv_ping_buf;
	rs_block p_rsts[ 2 ];
	common_user_data p_control;
	common_user_data p_timers[ SESSION_TIMER_NUM ];
	common_user_data p_op;
	common_user_data p_io;
	common_user_data p_ping;
	double_list_s stream_listeners;
	void * p_listener_head;
	pthread_mutex_t op_mutex;
}session_s;
typedef session_s * session;

enum{
	STREAM_MSS = 8 * 1024,
	INITIAL_WINDOW_SIZE = 4 * STREAM_MSS,
	INITIAL_BUF_SIZE = 2 * STREAM_MSS,
	FRAME_DROP_BUFFER_SIZE = 1024
};

//memory manager
enum{
	SESSION_MEM_LEVEL = 16
};

typedef struct{
	uint64_t used;
	uint64_t level[ SESSION_MEM_LEVEL ];//level越小级别越高
} session_mem_manager_s;
typedef session_mem_manager_s * session_mem_manager;

static session_mem_manager_s mem_manager;
//

STATIC_BEGAIN_INTERFACE( stream_hub )
STATIC_DECLARE_INTERFACE( io_pipe_interface_s )
STATIC_DECLARE_INTERFACE( gc_interface_s )
STATIC_DECLARE_INTERFACE( pipe_listener_interface_s )
STATIC_DECLARE_INTERFACE( timer_listener_interface_s )
STATIC_END_DECLARE_INTERFACE( stream_hub, 4 )
STATIC_GET_INTERFACE( stream_hub, gc_interface_s, 0 ) = {
	.ref_inc = stream_ref_inc,
	.ref_dec = stream_ref_dec
}
STATIC_GET_INTERFACE( stream_hub, io_pipe_interface_s, 1 ) = {
	.send = stream_send,
	.recv = stream_recv,
	.control = stream_control
	.close = stream_close
}
STATIC_GET_INTERFACE( stream_hub, timer_listener_interface_s, 2 ) = {
	.times_up = stream_timer_func
}
STATIC_GET_INTERFACE( stream_hub, pipe_listener_interface_s, 3 ) = {
	.free_pipe_data = stream_pipe_data_free,
	.get_pipe_data_len = stream_get_pipe_data_len,
	.close = stream_pipe_close
}
STATIC_END_INTERFACE( NULL )

STATIC_BEGAIN_INTERFACE( session_hub )
STATIC_DECLARE_INTERFACE( io_listener_interface_s )
STATIC_DECLARE_INTERFACE( session_interface_s )
STATIC_DECLARE_INTERFACE( gc_interface_s )
STATIC_DECLARE_INTERFACE( pipe_listener_interface_s )
STATIC_DECLARE_INTERFACE( timer_listener_interface_s )
STATIC_END_DECLARE_INTERFACE( session_hub, 5 )
STATIC_GET_INTERFACE( session_hub, gc_interface_s, 0 ) = {
	.ref_inc = session_ref_inc,
	.ref_dec = session_ref_dec
}
STATIC_GET_INTERFACE( session_hub, io_listener_interface_s, 1 ) = {
	.can_recv = session_recv_event,
	.can_send = session_send_event,
	.close = session_pipe_close
}
STATIC_GET_INTERFACE( session_hub, timer_listener_interface_s, 2 ) = {
	.times_up = session_timer_func
}
STATIC_GET_INTERFACE( session_hub, session_interface_s, 3 ) = {
	.link_io = session_link_io,
	.new_stream = session_new_stream,
}
STATIC_GET_INTERFACE( session_hub, pipe_listener_interface_s, 4 ) = {
	.free_pipe_data = session_pipe_data_free,
	.get_pipe_data_len = session_get_pipe_data_len,
	.close = session_pipe_close
}
STATIC_END_INTERFACE( NULL )

//level 15 - 10: 1/4; level 9 - 8: 1/2; level 7 - 0: 递增1/16
void set_session_mem( uint64_t mem_num )
{
	uint64_t u64;
	int i;
	
	for( i = 10; i < SESSION_MEM_LEVEL; i++ ){
		mem_manager.level[ i ] = mem_manager >> 2;// 1/4
	}
	mem_manager.level[ 9 ] = mem_num >> 1;// 1/2
	mem_manager.level[ 8 ] = mem_num >> 1; 
	u64 = mem_num >> 4;// 1/16
	for( i = 7; i > 0; i-- ){
		mem_manager.level[ i ] = mem_manager.level[ i + 1 ] + u64;
	}
	mem_manager.level[ 0 ] = mem_num;
}

//ret: = 0 ok, < 0 error
static inline int get_mem( int level_num, uint64_t min, uint64_t max, uint64_t * p_value )
{
	uint64_t u64, u64_tmp, level;

	if( level_num < SESSION_MEM_LEVEL
		&& ( level = mem_manager.level[ level_num ] ) >= min )
		do{
			if( mem_manager.used <= level - min ){
				u64 = mem_manager.used;
				u64_tmp = level - u64 > max ? max : level - u64;
			}else{
				return -1;
			}	
		}while( !__sync_bool_compare_and_swap( &mem_manager.used, u64, u64 + u64_tmp ) );
		 *p_value = u64_tmp;
		 return 0;
	}
	return -1;
}

static inline void put_mem( uint64_t mem_num )
{
	__sync_fetch_and_sub( &mem_manager.used, mem_num );
}

static int stream_level_map[ 16 ] = { 0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4 };
//first get form stream, then mem_manager, last malloc
static inline int stream_get_mem( stream p_stream, uint64_t min, uint64_t max, uint64_t * p_value )
{
	uint64_t u64, u64_tmp;
	int level_num, ret;
	
	level_num = 0;
	u64 = __sync_add_and_fetch( &p_stream->mem_used, max );
	if( u64 < 8 * 1024 * 1024 ){
		u64 >>= 14;
		if( u64 >= 16 ){
			level_num += 5;
			u64 >>= 5;
		}
		level_num += stream_level_map[ u64 ];
	}else{
		level_num = 10;
	}
	if( ( ret = get_mem( level_num, min, max, p_value ) ) == 0 ){
		u64_tmp = max - *p_value;
	}else{
		u64_tmp = max;
	}
	if( u64_tmp > 0 ){
		__sync_sub_and_fetch( &p_stream->mem_used, u64_tmp );
	}
	return ret;
}

//fist free, then put to mem_manager ,last stream
static inline void stream_put_mem( stream p_stream, uint64_t mem_num )
{
	put_mem( mem_num );
	__sync_fetch_and_sub( &p_stream->mem_used, mem_num );
}

static int output_packet_pri_compare( void * p0, void * p1 )
{
	uint64_t u64_0, u64_1, 
	int cmp;

	u64_0 = ( ( output_packet )p0 )->pri_seq;
	u64_1 = ( ( output_packet )p1 )->pri_seq;
	if( ( cmp = ( u64_0 >> SEQ_BIT_LEN ) - ( u64_1 >> SEQ_BIT_LEN ) ) == 0 ){
		cmp = ( ( u64_0 >> ( SEQ_BIT_LEN - 2 ) ) & 0x3 ) 
			| ( ( u64_1 >> ( SEQ_BIT_LEN - 4 ) ) & 0xc );
		if( u64_0 < u64_1 ){
			cmp = -1 + ( ( cmp == 2 || cmp == 3 ) << 1 );
		}else if( u64_0 > u64_1 ){
			cmp = 1 - ( ( cmp == 8 || cmp == 12 ) << 1 )
		}else{
			cmp = 0;
		}
	}
	return cmp;
}

int session_new( void * p_pipe, int is_server )
{
	session p_session;
	char * buf;
	rs_buf p_buf_head;
	rs_block p_block;
	timer_desc_s desc;
	common_user_data p_user_data;
	pipe_interface p_pipe_i;
	int i, j, len, i_len, timer_len, timer_point_len, op_len;
	void * p_timer, pipe[ 2 ], p_save_pipe[ SESSION_PIPE_NUM ];

	if( __builtin_expect( p_pipe == NULL, 0 ) ){
		return -1;
	}
	p_pipe_i = FIND_INTERFACE( p_pipe, pipe_interface_s );
	timer_point_len = 0;
	p_timer = get_timer_instance();
	CALL_INTERFACE_FUNC( p_timer, pipe_interface_s, get_pipe_data_len, &timer_point_len );
	timer_len = calculate_pipe_len( SESSION_TIMER_LEN, timer_point_len );
	op_len = calculate_pipe_len( SESSION_OP_LEN, SESSION_AVL_LEN );
	i_len = CACULATE_INTERFACE_ENTITY_LEN( 0, 0 );
	len = ROUND_UP( i_len + sizeof( *p_session ), BUF_ALIGN ) 
		+ ROUND_UP( SESSION_RST_LEN, BUF_ALIGN ) * 2 
		+ ROUND_UP( op_len, BUF_ALIGN )
		+ ROUND_UP( timer_len, BUF_ALIGN ) * SESSION_TIMER_NUM
		+ BUF_ALIGN;
	buf = session_rs_buf_malloc( len, len, &len );
	if( __builtin_expect( buf == NULL, 0 ) ){
		MOON_PRINT_MAN( ERROR, "session malloc error!" );
		goto malloc_error;
	}
	memset( buf, 0, len );
	p_buf_head = ( rs_buf )buf - 1;
	buf = ( char * )ROUND_UP( ( uintptr_t )buf, BUF_ALIGN );
	//session init
	p_session = ( session )( buf + i_len );
	p_session->offset = POINT_OFFSET( p_session, p_buf_head );
	if( __builtin_expect( pthread_mutex_init( &p_session->op_mutex, NULL ) != 0, 0 ) ){
		MOON_PRINT_MAN( ERROR, "init op mutex fail!" );
		session_rsbuf_free( p_buf_head );
		goto malloc_error;
	}
	INIT_INTERFACE_ENTITY( buf, p_session );
	BEGAIN_INTERFACE( p_session );
	END_INTERFACE( GET_STATIC_INTERFACE_HANDLE( &session_hub ) );
	p_session->p_listener_head = list_to_data( &p_session->stream_listeners );
	p_session->ref_num = 1;
	//服务端序列号为偶数，客户端为奇数
	if( is_server ){
		p_session->is_server = 1;
		p_session->next_stream_id = 2;
		p_session->last_ping_unique_id = 0;
	}else{
		p_session->next_stream_id = 1;
		p_session->last_ping_unique_id = 1;
	}
	p_session->init_status = SESSION_UNINIT;
	buf += ROUND_UP( i_len + sizeof( *p_session ), BUF_ALIGN ); 
	//rst frame init
	for( i = 0; i < 2; i++ ){
		p_block = ( rs_block )buf;
		p_session->p_rsts[ i ] = p_block;
		rs_buf_ref_inc( p_buf_head );
		p_block->offset = POINT_OFFSET( p_block, p_buf_head );
		p_block->flags = IS_HEAD | IS_TAIL | IS_READY;
		buf += ROUND_UP( SESSION_RST_LEN, BUF_ALIGN );
	}
	p_session->rst_num = SESSION_MAX_RST_MUN;
	p_session->next_buf = ( char * )( p_block->p_rsts[ 0 ] + 1 );
	//timers init
	for( i = SESSION_RECV_TIMER; i < SESSION_TIMER_NUM; i++ ){
		ret = pipe_new2( pipe, SESSION_TIMER_LEN, timer_point_len, 1, buf );
		if( __builtin_expect( ret < 0, 0 ) ){
			goto set_error;
		}
		rs_buf_ref_inc( p_buf_head );
		p_user_data = pipe[ 0 ];
		p_user_data[ 0 ].i32[ 0 ] = i;
		p_user_data[ 0 ].i32[ 1 ] = PIPE_STATUS_ACTIVE;
		p_user_data[ 1 ].i32[ 0 ] = POINT_OFFSET( p_user_data, p_buf_head );
		p_session->p_timers[ i ] = p_user_data;
		CALL_INTERFACE_FUNC( pipe[ 0 ], pipe_interface_s, set_point_ref, p_session );
		CALL_INTERFACE_FUNC( pipe[ 0 ], gc_interface_s, ref_inc );
		p_save_pipe[ i ] = pipe[ 1 ];
		buf += ROUND_UP( timer_len, BUF_ALIGN );
	}
	//op init
	ret = pipe_new2( pipe, SESSION_OP_LEN, SESSION_AVL_LEN, 1, buf );
	if( __builtin_expect( ret < 0, 0 ) ){
		goto set_error;
	}
	rs_buf_ref_inc( p_buf_head );
	p_user_data = pipe[ 0 ];
	p_user_data[ 0 ].i32[ 0 ] = SESSION_OP;
	p_user_data[ 0 ].i32[ 1 ] = PIPE_STATUS_ACTIVE;
	p_user_data[ 1 ].i32[ 0 ] = POINT_OFFSET( p_user_data, p_buf_head );
	p_session->p_op = p_user_data;
	CALL_INTERFACE_FUNC( pipe[ 0 ], pipe_interface_s, set_point_ref, p_session );
	CALL_INTERFACE_FUNC( pipe[ 0 ], gc_interface_s, ref_inc );
	p_save_pipe[ i ] = pipe[ 1 ];
	buf += ROUND_UP( op_len, BUF_ALIGN );
	i++:

	desc.repeat_num = 0;
	for( i = SESSION_RECV_TIMER; i < SESSION_TIMER_NUM; i++ ){
		new_timer( p_timer, p_save_pipe[ i ], &desc );
		CALL_INTERFACE_FUNC( p_save_pipe[ i ], gc_interface_s, ref_dec );
	}
	init_op_to_session( p_session, p_save_pipe[ i ] );
	CALL_INTERFACE_FUNC( p_save_pipe[ i ], gc_interface_s, ref_dec );

	p_user_data = p_pipe;
	p_user_data[ 0 ].i32[ 0 ] = SESSION_CONTROL;
	p_user_data[ 0 ].i32[ 1 ] =PIPE_STATUS_ACTIVE;
	CALL_INTERFACE_FUNC( p_pipe, gc_interface_s, ref_inc );
	p_session->p_control = p_user_data;
	p_pipe_i->set_point_ref( p_pipe, p_session );
	p_pipe_i->init_done( p_pipe, 0 );
	session_ref_dec( p_session );
	return 0;
set_error:
	for( j = SESSION_RECV_TIMER; j < i; j++ ){
		CALL_INTERFACE_FUNC( p_save_pipe[ j ], pipe_interface_s, init_done, 1 );
		CALL_INTERFACE_FUNC( p_save_pipe[ j ], gc_interface_s, ref_dec );
	}
	close_session( p_session );
	session_ref_dec( p_session );
malloc_error:
	p_pipe_i->init_done( p_pipe, 1 );
	return -1;
}

static void session_pipe_data_free( void * p_pipe )
{
	common_user_data p_user_data;

	p_user_data = p_pipe;
	switch( p_user_data->i32[ 0 ] ){
	case SESSION_RECV_TIMER:
	case SESSION_SEND_TIMER:
	case SESSION_OP:
		session_rsbuf_free( ( rs_buf )( ( char * )p_user_data - p_user_data[ 1 ].i32[ 0 ] ) );
		break;
	default:
	}
}

static void session_free( session p_session )
{
	output_packet_s packet;

	//op_mutex
	pthread_mutex_destroy( &p_session->op_mutex );
	//free session
	session_rsbuf_free( ( rs_buf )( ( char * )p_session - p_session->offset ) );
}

static int stream_cmp( void * p_data, common_user_data_u user_data )
{
	int id0, id1;

	id0 = ( ( common_user_data )p_data )->i_num;
	id1 = user_data.i_num;
	return id0 - id1;
}

static int init_op_to_session( session p_session, void * p_pipe )
{
	common_user_data p_user_data;
	avl_tree p_avl;

	p_pipe_i = FIND_INTERFACE( p_pipe, pipe_interface_s );
	p_user_data = p_pipe;
	p_user_data->i32[ 0 ] = SESSION_OP_HUB;
	p_user_data->i32[ 1 ] = PIPE_STATUS_STOP;
	p_pipe_i->set_point_ref( p_pipe, p_session );
	p_pipe_i->init_done( p_pipe );
	return 0;
}

static int add_stream_to_session( session p_session, void * p_pipe, int stream_id )
{
	common_user_data p_user_data;
	avl_tree p_avl;
	common_user_data_u user_data;
	pipe_interface p_pipe_i;
	int ret;

	ret = -1;
	p_pipe_i = FIND_INTERFACE( p_pipe, pipe_interface_s );
	p_user_data = p_pipe;
	p_user_data->i32[ 0 ] = SESSION_LINK_STREAM;
	p_user_data->i32[ 1 ] = PIPE_STATUS_DEL;
	p_avl = ( avl_tree )( p_user_data + 1 );
	p_user_data = ( common_user_data )( p_avl + 1 );
	p_user_data->i_num = stream_id;
	user_data.i_num = stream_id;
	p_pipe_i->set_point_ref( p_pipe, p_session );
	pthread_mutex_lock( &p_session->op_mutex );
	if( !IS_SESSION_CLOSED( p_session ) ){
		ret = 0;
		( ( common_user_data )p_pipe )->i32[ 1 ] = PIPE_STATUS_ACTIVE;
		CALL_INTERFACE_FUNC( p_pipe, gc_interface_s, ref_inc );
		avl_add2( &p_session->p_streams_avl, p_user_data, stream_cmp, user_data );
	}
	pthread_mutex_unlock( &p_session->op_mutex );
	p_pipe_i->init_done( p_pipe, -ret );
	return ret;
}

static int add_pushed_stream( stream p_stream, void * p_pipe )
{
	int ret;
	common_user_data p_user_data;
	pipe_interface p_pipe_i;

	ret = -1;
	p_pipe_i = FIND_INTERFACE( p_pipe, pipe_interface_s );
	p_user_data = p_pipe;
	p_user_data->i32[ 0 ] = STREAM_PUSHED;
	p_user_data->i32[ 1 ] = PIPE_STATUS_DEL;
	p_pipe_i->set_point_ref( p_pipe, p_stream );
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( !IS_STREAM_CLOSED( p_stream ) ){
		ret = 0;
		p_user_data->i32[ 1 ] = PIPE_STATUS_ACTIVE;
		CALL_INTERFACE_FUNC( p_pipe, gc_interface_s, ref_inc );
		dlist_append( p_stream->p_pushed_head, list_to_data( &p_user_data[ 2 ] ) );
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	p_pipe_i->init_done( p_pipe, -ret );
	return ret;
}

static stream stream_new( session p_session, stream p_assoc_stream, int stream_id, int pri, int is_pushed )
{
	stream p_stream;
	char * buf;
	rs_buf p_buf_head;
	rs_block p_block;
	timer_desc_s desc;
	common_user_data p_user_data;
	output_packet p_out;
	pipe_interface p_pipe_i;
	int i, j, link_stream_len, control_len, link_session_len;
	int len, i_len, timer_len, timer_point_len, control_point_len;
	void * p_timer, * pipe[ 2 ], * p_tmp;
	void * p_point, *p_point_data, *p_save_pipe[ STREAM_PIPE_NUM ];

	p_tmp = NULL;
	control_point_len = timer_point_len = 0;
	p_timer = get_timer_instance();
	CALL_INTERFACE_FUNC( p_timer, pipe_interface_s, get_pipe_data_len, &timer_point_len );
	timer_len = calculate_pipe_len( SESSION_TIMER_LEN, timer_point_len );
	link_stream_len = 0;
	if( is_pushed ){
		link_stream_len = calculate_pipe_len( STREAM_PIPE_LEN, STREAM_PIPE_LEN );
	}
	link_session_len = calculate_pipe_len( SESSION_OP_LEN, SESSION_AVL_LEN );
	i_len = CACULATE_INTERFACE_ENTITY_LEN( 0, 0 );
	if( ( stream_id % 2 ) == p_session->is_server//对方的需要回调
		|| p_assoc_stream == NULL ){//自己的但非关联需要回调
		if( p_assoc_stream != NULL ){
			pthread_mutex_lock(	&p_assoc_stream->deferred_mutex );
			if( CAN_STREAM_ASSOC( p_assoc_stream ) ){
				p_tmp = p_assoc_stream->p_control;
				CALL_INTERFACE_FUNC( p_tmp, gc_interface_s, ref_inc );
			}
			pthread_mutex_unlock( &p_assoc_stream->deferred_mutex );
		}else{
			pthread_mutex_lock( &p_session->op_mutex );
			if( !IS_SESSION_CLOSED( p_session ) ){
				p_tmp = p_session->p_control;
				CALL_INTERFACE_FUNC( p_tmp, gc_interface_s, ref_inc );
			}
			pthread_mutex_unlock( &p_session->op_mutex );
		}
		if( p_tmp != NULL ){
			p_pipe_i = FIND_INTERFACE( p_tmp, pipe_interface_s );
			if( p_pipe_i->get_other_point_ref( p_tmp, &p_point, &p_point_data ) == 0 ){
				CALL_INTERFACE_FUNC( p_point
					, pipe_listener_interface_s, get_pipe_data_len, &control_point_len );
				CALL_INTERFACE_FUNC( p_point, gc_interface_s, ref_dec );
				CALL_INTERFACE_FUNC( p_point_data, gc_interface_s, ref_dec );
				control_len = calculate_pipe_len( STREAM_PIPE_LEN, control_point_len );
			}else{
				CALL_INTERFACE_FUNC( p_tmp, gc_interface_s, ref_dec );
				return NULL;
			}
		}else{
			return NULL;
		}
	}
	len = ROUND_UP( i_len + sizeof( *p_stream ), BUF_ALIGN )
		+ ROUND_UP( STREAM_WIN_UPDATE_LEN, BUF_ALIGN )
		+ ROUND_UP( STREAM_RST_LEN , BUF_ALIGN )
		+ ROUND_UP( timer_len, BUF_ALIGN ) * STREAM_TIMER_NUM
		+ ROUND_UP( link_session_len, BUF_ALIGN ) * 3
		+ ROUND_UP( link_stream_len, BUF_ALIGN )
		+ ROUND_UP( control_point_len, BUF_ALIGN )
		+ BUF_ALIGN;
	buf = session_rs_buf_malloc( len, len, &len );
	if( __builtin_expect( buf == NULL, 0 ) ){
		MOON_PRINT_MAN( ERROR, "stream malloc error!" );
		return NULL;
	}
	memset( buf, 0, len );
	p_buf_head = ( rs_buf )buf - 1;
	buf = ( char * )ROUND_UP( ( uintptr_t )buf, BUF_ALIGN );
	//stream init
	p_stream = ( stream )( buf + i_len );
	p_stream->offset = POINT_OFFSET( p_stream, p_buf_head );
	if( __builtin_expect( pthread_mutex_init( &p_stream->deferred_mutex, NULL ) != 0, 0 ) ){
		MOON_PRINT_MAN( ERROR, "init deferred mutex fail!" );
		session_rsbuf_free( p_buf_head );
		return NULL;
	}
	INIT_INTERFACE_ENTITY( buf, p_stream );
	BEGAIN_INTERFACE( p_stream );
	END_INTERFACE( GET_STATIC_INTERFACE_HANDLE( &stream_hub ) );
	p_stream->p_pushed_head = list_to_data( &p_stream->pushed_streams );
	p_stream->ref_num = 1;
	p_stream->status = STREAM_INITIAL;
	p_stream->stream_id = stream_id;
	p_stream->pri = pri;
	p_stream->is_pushed = is_pushed;
	p_stream->send_window_size = INITIAL_WINDOW_SIZE;
	p_stream->send_buf_limit = INITIAL_BUF_SIZE;
	p_stream->send_buf_size = INITIAL_BUF_SIZE;
	p_stream->recv_window_size = INITIAL_WINDOW_SIZE;
	p_stream->recv_window_base = INITIAL_WINDOW_SIZE;
	buf += ROUND_UP( i_len + sizeof( *p_stream ), BUF_ALIGN );
	//window update frame init
	p_block = ( rs_block )buf;
 	p_stream->p_window_update = p_block;
	rs_buf_ref_inc( p_buf_head );
	p_block->offset = POINT_OFFSET( p_block, p_buf_head );
	p_block->len = FRAME_WINDOW_UPDATE_LEN;
	p_block->flags = IS_HEAD | IS_TAIL | IS_READY;
	buf += ROUND_UP( STREAM_WIN_UPDATE_LEN, BUF_ALIGN );
	//rst frame init
	p_block = ( rs_block )buf;
	p_stream->p_rst = p_block;
	rs_buf_ref_inc( p_buf_head );
	p_block->offset = POINT_OFFSET( p_block, p_buf_head );
	p_block->len = FRAME_RST_LEN;
	p_block->flags = IS_HEAD | IS_TAIL | IS_READY;
	buf += ROUND_UP( STREAM_RST_LEN, BUF_ALIGN );
	//timers init
	for( i = STREAM_RECV_TIMER; i < STREAM_TIMER_NUM; i++ ){
		ret = pipe_new2( pipe, SESSION_TIMER_LEN, timer_point_len, 1, buf );
		if( __builtin_expect( ret < 0, 0 ) ){
			goto error;
		}
		rs_buf_ref_inc( p_buf_head );
		p_user_data = pipe[ 0 ];
		p_user_data[ 0 ].i32[ 0 ] = i;
		p_user_data[ 0 ].i32[ 1 ] = PIPE_STATUS_ACTIVE;
		p_user_data[ 1 ].i32[ 0 ] = POINT_OFFSET( p_user_data, p_buf_head );
		p_stream->p_timers[ i ] = p_user_data;
		CALL_INTERFACE_FUNC( pipe[ 0 ], pipe_interface_s, set_point_ref, p_stream );
		CALL_INTERFACE_FUNC( pipe[ 0 ], gc_interface_s, ref_inc );
		p_save_pipe[ i ] = pipe[ 1 ];
		buf += ROUND_UP( timer_len, BUF_ALIGN );
	}
	//ops init
	for( j = 0; j <= STREAM_OP_NUM; j++, i++ ){
		ret = pipe_new2( pipe, SESSION_OP_LEN, SESSION_AVL_LEN, 1, buf );
		if( __builtin_expect( ret < 0, 0 ) ){
			goto error;
		}
		rs_buf_ref_inc( p_buf_head );
		p_user_data = pipe[ 0 ];
		p_user_data[ 0 ].i32[ 0 ] = STREAM_OP + j;
		p_user_data[ 0 ].i32[ 1 ] = PIPE_STATUS_ACTIVE;
		p_user_data[ 1 ].i32[ 0 ] = POINT_OFFSET( p_user_data, p_buf_head );
		p_stream->p_ops[ j ] = pipe[ 0 ];
		CALL_INTERFACE_FUNC( pipe[ 0 ], pipe_interface_s, set_point_ref, p_stream );
		CALL_INTERFACE_FUNC( pipe[ 0 ], gc_interface_s, ref_inc );
		p_save_pipe[ i ] = pipe[ 1 ];
		buf += ROUND_UP( link_session_len, BUF_ALIGN );
	}
	//add to session
	ret = pipe_new2( pipe, SESSION_OP_LEN, SESSION_AVL_LEN, 1, buf );
	if( __builtin_expect( ret < 0, 0 ) ){
		goto error;
	}
	rs_buf_ref_inc( p_buf_head );
	p_user_data = pipe[ 0 ];
	p_user_data[ 0 ].i32[ 0 ] = STREAM_SESSION;
	p_user_data[ 0 ].i32[ 1 ] = PIPE_STATUS_ACTIVE;
	p_user_data[ 1 ].i32[ 0 ] = POINT_OFFSET( p_user_data, p_buf_head );
	p_stream->p_session_pipe = p_user_data;
	CALL_INTERFACE_FUNC( pipe[ 0 ], pipe_interface_s, set_point_ref, p_stream );
	CALL_INTERFACE_FUNC( pipe[ 0 ], gc_interface_s, ref_inc );
	p_save_pipe[ i ] = pipe[ 1 ];
	buf += ROUND_UP( link_session_len, BUF_ALIGN );
	i++;
	//assoic stream
	if( is_pushed ){
		ret = pipe_new2( pipe, STREAM_PIPE_LEN, STREAM_PIPE_LEN, 1, buf );
		if( __builtin_expect( ret < 0, 0 ) ){
			goto error;
		}
		rs_buf_ref_inc( p_buf_head );
		p_user_data = pipe[ 0 ];
		p_user_data[ 0 ].i32[ 0 ] = STREAM_ROOT;
		p_user_data[ 0 ].i32[ 1 ] = PIPE_STATUS_ACTIVE;
		p_user_data[ 1 ].i32[ 0 ] = POINT_OFFSET( p_user_data, p_buf_head );
		dlist_append( p_stream->p_pushed_head, list_to_data( &p_user_data[ 2 ] ) );
		CALL_INTERFACE_FUNC( pipe[ 0 ], pipe_interface_s, set_point_ref, p_stream );
		CALL_INTERFACE_FUNC( pipe[ 0 ], gc_interface_s, ref_inc );
		p_save_pipe[ i ] = pipe[ 1 ];
		buf += ROUND_UP( link_stream_len, BUF_ALIGN );
		i++;
	}
	//control pipe
	if( control_len > 0 ){
		ret = pipe_new2( pipe, STREAM_PIPE_LEN, control_point_len, 1, buf );
		if( __builtin_expect( ret < 0, 0 ) ){
			goto error;
		}
		rs_buf_ref_inc( p_buf_head );
		p_user_data = pipe[ 0 ];
		p_user_data[ 0 ].i32[ 0 ] = STREAM_CONTROL;
		p_user_data[ 0 ].i32[ 1 ] = PIPE_STATUS_ACTIVE;
		p_user_data[ 1 ].i32[ 0 ] = POINT_OFFSET( p_user_data, p_buf_head );
		p_stream->p_control = p_user_data;
		CALL_INTERFACE_FUNC( pipe[ 0 ], pipe_interface_s, set_point_ref, p_stream );
		CALL_INTERFACE_FUNC( pipe[ 0 ], gc_interface_s, ref_inc );
		p_save_pipe[ i ] = pipe[ 1 ];
		buf += ROUND_UP( control_len, BUF_ALIGN );
		i++;
	}

	desc.repeat_num = 0;
	for( i = STREAM_RECV_TIMER; i < STREAM_TIMER_NUM; i++ ){
		new_timer( p_timer, p_save_pipe[ i ], &desc );
		CALL_INTERFACE_FUNC( p_save_pipe[ i ], gc_interface_s, ref_dec );
	}
	for( j = i + STREAM_OP_NUM; i < j; i++ ){
		init_op_to_session( p_session, p_save_pipe[ i ] );
		CALL_INTERFACE_FUNC( p_save_pipe[ i ], gc_interface_s, ref_dec );
	}
	add_stream_to_session( p_session, p_save_pipe[ i ], stream_id );
	CALL_INTERFACE_FUNC( p_save_pipe[ i ], gc_interface_s, ref_dec );
	i++;
	if( is_pushed ){
		add_pushed_stream( p_assoc_stream, p_save_pipe[ i ] );
		CALL_INTERFACE_FUNC( p_save_pipe[ i ], gc_interface_s, ref_dec );
		i++;
	}
	if( ( stream_id % 2 ) == p_session->is_server ){
		p_stream->status = STREAM_SYN_RECV;
	}
	if( control_len > 0 ){
		CALL_PIPE_POINT_FUNC_RET( p_tmp
			, order_listener_interface_s, on_ready, p_save_pipe[ i ] );
		CALL_INTERFACE_FUNC( p_save_pipe[ i ], gc_interface_s, ref_dec );
		CALL_INTERFACE_FUNC( p_tmp, gc_interface_s, ref_dec );
	}
	return p_stream;
error:
	for( j = 0; j < i; j++ ){
		CALL_INTERFACE_FUNC( p_save_pipe[ j ], pipe_interface_s, init_done, 1 );
		CALL_INTERFACE_FUNC( p_save_pipe[ j ], gc_interface_s, ref_dec );
	}
	inter_error_stream( p_stream );
	stream_ref_dec( p_stream );
	return NULL;
}

#define STREAM_START_SENDTASK( p_stream ) ({\
	int _ret = 0;\
	\
	if( ( p_stream->shut_flags & STREAM_CAN_SEND ) == STREAM_CAN_SEND\
		&& p_stream->send_buf_size >= STREAM_MSS\
		&& p_stream->send_window_size >= STREAM_MSS ){\
		p_stream->shut_flags &= ~STREAM_CAN_RECV;\
		_ret = 1;\
	}\
	_ret;\
})

static int user_set_can_send( stream p_stream, int timeout_ms )
{
	int ret, can_send, mark_id;
	timer_desc_s desc;
	common_user_data p_timer, p_control;
	common_user_data_u user_data;

	ret = -1;
	user_data.i_num = can_send = 0;
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( CAN_STREAM_SET_SEND( p_stream ) ){
		ret = 0;
		p_stream->shut_flags |= STREAM_CAN_SEND;
		//清掉recv timer
		p_timer = p_stream->p_timers[ STREAM_SEND_TIMER ];
		p_timer[ 1 ].i32[ 1 ]++;//change mark_id
		mark_id = p_timer[ 1 ].i32[ 1 ];
		if( ( can_send = STREAM_START_SENDTASK( p_stream ) ) != 0 ){
			//获取用户句柄
			p_control = p_stream->p_control;
			CALL_INTERFACE_FUNC( p_control, gc_interface_s, ref_inc );
		}else if( timeout_ms > 0 ){
			CALL_INTERFACE_FUNC( p_timer, gc_interface_s, ref_inc );
		}
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( can_send ){
		CALL_PIPE_POINT_FUNC( p_control
			, io_listener_interface_s, send_event, user_data );
		CALL_INTERFACE_FUNC( p_control, gc_interface_s, ref_dec );
	}else if( ret == 0
			&& timeout_ms > 0 ){
		desc.mark_id = mark_id;
		desc.repeat_num = 1;
		desc.time_us = timeout_ms * 1000;
		ret = -1;
		CALL_PIPE_POINT_FUNC_RET( ret, p_timer, timer_interface_s, set, &desc );
		CALL_INTERFACE_FUNC( p_timer, gc_interface_s, ref_dec );
		if( ret < 0 ){
			ret = 0;
			pthread_mutex_lock( &p_stream->deferred_mutex );
			if( CAN_STREAM_SET_SEND( p_stream ) ){
				p_timer = p_stream->p_timers[ STREAM_SEND_TIMER ];
				if( p_timer[ 1 ].i32[ 1 ] == mark_id ){
					ret = -1;
					p_stream->shut_flags &= ~STREAM_SEND_TIMER;
				}
			}
			pthread_mutex_unlock( &p_stream->deferred_mutex );
		}
	}
	return ret;
}

static int stream_send( void * p_data, void * p_pipe, char * buf, int len, int flags, ... )
{
	char * ptr;
	stream p_stream;
	int ret, offset, tmp, body_len, head_len, malloc_len;
	rs_block p_block, p_block_tmp;
	rs_buf_hub p_hub;
	common_user_data p_user_data;

	if( p_data == NULL || p_pipe == NULL || buf == NULL || len <=0 ){
		return SOCKET_PARAERROR;
	}
	ret = SOCKET_CLOSED;
	p_stream = p_data;
	p_user_data = p_pipe;
	p_hub = &p_stream->send_hub;
	body_len = head_len = 0;
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( CAN_STREAM_SET_SEND( p_stream )
		&& p_user_data->i32[ 1 ] == PIPE_STATUS_ACTIVE ){
		if( p_hub->p_last_packet_head != NULL ){//append
			body_len = MIN( len, STREAM_MSS - p_hub->last_packet_len );
		}else{//new
			body_len = MIN( len, STREAM_MSS );
			head_len = FRAME_HEAD_LEN;
			if( p_stream->status == STREAM_INITIAL ){
				head_len = FRAME_SYN_HEAD_LEN;
			}
		}
		total_len = head_len + body_len;
		if( head_len + body_len >= BUF_BASE_ACTUAL_SIZE ){
			malloc_len = head_len + body_len;
		}
		if( total_len <= p_hub->common_buf_left ){
			p_block = p_hub->p_common_buf;
			ptr = ( char *)( p_block + 1 ) + p_block->len + head_len;
			memcpy( ptr, buf, body_len );
			p_block->len += total_len;
		}
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
}

#define STREAM_START_RECVTASK( p_stream ) ({\
	int _ret = 0;\
	rs_block _p_block = p_stream->recv_hub.p_head;\
	\
	if( ( p_stream->shut_flags & STREAM_CAN_RECV ) == STREAM_CAN_RECV\
		&& _p_block != NULL\
		&& ( _p_block->flags & IS_READY ) == IS_READY ){\
		p_stream->shut_flags &= ~STREAM_CAN_RECV;\
		_ret = 1;\
	}\
	_ret;\
})

//多线程同时调用user_set_can_recv时会出现时序不一致，前面的壶盖后面的
//只要将timer接口扩充为支持版本控制，但这样做好像没有多大意义
static int user_set_can_recv( stream p_stream, int timeout_ms )
{
	int ret, can_recv, mark_id;
	timer_desc_s desc;
	common_user_data p_timer, p_control;
	common_user_data_u user_data;

	ret = -1;
	user_data.i_num = can_recv = 0;
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( CAN_STREAM_SET_RECV( p_stream ) ){
		ret = 0;
		p_stream->shut_flags |= STREAM_CAN_RECV;
		//清掉recv timer
		p_timer = p_stream->p_timers[ STREAM_RECV_TIMER ];
		p_timer[ 1 ].i32[ 1 ]++;//change mark_id
		mark_id = p_timer[ 1 ].i32[ 1 ];
		if( ( can_recv = STREAM_START_RECVTASK( p_stream ) ) != 0 ){
			//获取用户句柄
			p_control = p_stream->p_control;
			CALL_INTERFACE_FUNC( p_control, gc_interface_s, ref_inc );
		}else if( timeout_ms > 0 ){
			CALL_INTERFACE_FUNC( p_timer, gc_interface_s, ref_inc );
		}
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( can_recv ){
		CALL_PIPE_POINT_FUNC( p_control
			, io_listener_interface_s, recv_event, user_data );
		CALL_INTERFACE_FUNC( p_control, gc_interface_s, ref_dec );
	}else if( ret == 0
			&& timeout_ms > 0 ){
		desc.mark_id = mark_id;
		desc.repeat_num = 1;
		desc.time_us = timeout_ms * 1000;
		ret = -1;
		CALL_PIPE_POINT_FUNC_RET( ret, p_timer, timer_interface_s, set, &desc );
		CALL_INTERFACE_FUNC( p_timer, gc_interface_s, ref_dec );
		if( ret < 0 ){
			ret = 0;
			pthread_mutex_lock( &p_stream->deferred_mutex );
			if( CAN_STREAM_SET_RECV( p_stream ) ){
				p_timer = p_stream->p_timers[ STREAM_RECV_TIMER ];
				if( p_timer[ 1 ].i32[ 1 ] == mark_id ){
					ret = -1;
					p_stream->shut_flags &= ~STREAM_CAN_RECV;
				}
			}
			pthread_mutex_unlock( &p_stream->deferred_mutex );
		}
	}
	return ret;
}

static int inter_add_packet_to_stream( stream p_stream, rs_block p_block, int len,  int ch_status )
{
	int ret, can_recv;
	common_user_data p_timer, p_control;
	common_user_data_u user_data;

	can_recv = 0;
	ret = -1;
	p_block_tmp = NULL;
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( CAN_STREAM_ADD_BUF( p_stream ) ){
		ret = 0;
		p_stream->status += ch_status;
		p_stream->recv_window_size -= len;
		append_to_tail( &p_stream->recv_hub, p_block );
		if( ( can_recv = STREAM_START_RECVTASK( p_stream ) ) != 0 ){
			//清掉recv timer
			p_timer = p_stream->p_timers[ STREAM_RECV_TIMER ];
			p_timer[ 1 ].i32[ 1 ]++;//change mark_id
			CALL_INTERFACE_FUNC( p_timer, gc_interface_s, ref_inc );
			//获取用户句柄
			p_control = p_stream->p_control;
			CALL_INTERFACE_FUNC( p_timer, gc_interface_s, ref_inc );
		}
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( can_recv ){
		CALL_PIPE_POINT_FUNC( p_timer, timer_interface_s, stop );
		user_data.i_num = 0;
		CALL_PIPE_POINT_FUNC( p_control
			, io_listener_interface_s, recv_event, user_data );
		CALL_INTERFACE_FUNC( p_timer, gc_interface_s, ref_dec );
		CALL_INTERFACE_FUNC( p_control, gc_interface_s, ref_dec );
	}
	return ret;
}

static int stream_recv( void * p_data, void * p_pipe, char * buf, int len, int flags, ... )
{
	char * ptr;
	stream p_stream;
	int ret, offset, fetch_len, tmp;
	rs_block p_block, p_block_tmp;
	common_user_data p_user_data;

	if( p_data == NULL || p_pipe == NULL || buf == NULL || len <=0 ){
		return SOCKET_PARAERROR;
	}
	ret = SOCKET_CLOSED;
	p_stream = p_data;
	p_user_data = p_pipe;
	fetch_len = 0;
	while( fetch_len < len ){
		pthread_mutex_lock( &p_stream->deferred_mutex );
		if( CAN_STREAM_SET_RECV( p_stream ) 
			&& p_user_data->i32[ 1 ] == PIPE_STATUS_ACTIVE ){
			ret = SOCKET_NO_RES;
			p_block = get_from_head_len( &p_stream->recv_hub, len - fetch_len, &offset );
			if( p_block != NULL
				&& p_stream->status == STREAM_RST_RECV
				&& p_stream->recv_hub.total_len == 0 ){
				p_stream->status = STREAM_SHUT_RECV;
			}
		}
		pthread_mutex_unlock( &p_stream->deferred_mutex );
		if( p_block == NULL ){
			break;
		}
		do{
			tmp = MIN( len - fetch_len, p_block->len - offset );
			ptr = ( char * )( p_block + 1 ) + offset;
			memcpy( buf, ptr, tmp );
			buf += tmp;
			fetch_len += tmp;
			p_block_tmp = p_block;
			p_block = p_block->next;
			stream_rsbuf_free( p_stream, BLOCK_TO_BUF( p_block_tmp ) );
			offset = 0;
		}while( p_block != NULL && fetch_len < len );
	}
	if( fetch_len > 0 ){
		ret = fetch_len;
	}
	return ret;
}

static int inter_add_packet_to_session( void * p_op, int pri )
{

}

static int stream_send_rst( void * p_rst, void * p_timer, int mark_id )
{
	int ret;
	timer_desc_s desc;

	if( ( ret = inter_add_packet_to_session( p_rst, HIGHEST_PRI ) ) == 0 ){
		desc.time_us = STREAM_RST_TIMEOUT_MS * 1000;
		desc.repeat_num = 1;
		desc.mark_id = mark_id;
		ret = -1;
		CALL_PIPE_POINT_FUNC_RET( ret, p_timer, timer_interface_s, set, &desc );
	}
	return ret;
}

static void stream_rshub_free( stream p_stream, rs_buf_hub p_hub )
{
	if( p_hub->head != NULL ){
		stream_rsblock_free( p_stream, p_hub->p_head );
	}
	if( p_hub->p_common_buf != NULL ){
		stream_rsblock_free( p_stream, p_hub->p_common_buf );
	}
}

static void stream_local_close( stream p_stream, int error_code )
{
	rs_buf_hub_s recv_hub, send_hub;
	common_user_data p_timers[ STREAM_TIMER_NUM ];
	common_user_data p_ops[ STREAM_OP_NUM ], p_user_data, p_control;
	int i, ret, closed, send_rst, clear_bits, mark_id;

	clear_bits = close = send_rst = 0;
	p_control = NULL;
	pthread_mutex_lock( &p_stream->deferred_mutex );
	switch( p_stream->status ){
	case STREAM_SYN_RECV:
	case STREAM_SYN_SEND:
	case STREAM_SYNREPLY_RS:
		send_rst = 1;
		p_stream->status = STREAM_RST_SEND;
		clear_bits = p_stream->shut_flags & ( STREAM_CAN_RECV | STREAM_CAN_SEND );
		p_stream->shut_flags &= ~clear_bits;
		p_control = p_stream->p_control;
		p_control->i32[ 1 ] = PIPE_STATUS_DEL;
		p_stream->p_control = NULL;
		recv_hub = p_stream->recv_hub;
		send_hub = p_stream->send_hub;
		memset( &p_stream->recv_hub, 0, sizeof( p_stream->recv_hub ) );
		memset( &p_stream->send_hub, 0, sizeof( p_stream->send_hub ) );
		for( i = 0; i < STREAM_TIMER_NUM; i++ ){
			p_user_data = p_stream->p_timers[ i ];
			p_timers[ i ] = p_user_data;
			p_user_data[ 1 ].i32[ 1 ]++;
			CALL_INTERFACE_FUNC( p_user_data, gc_interface_s, ref_inc );
		}
		p_user_data = p_timers[ STREAM_RST_TIMER ];
		mark_id = p_user_data[ 1 ].i32[ 1 ];
		for( i = 0; i < STREAM_OP_NUM - 1; i++ ){
			p_ops[ i ] = p_stream->p_ops[ i ];
			p_ops[ i ]->i32[ 1 ] = PIPE_STATUS_DEL;
			p_stream->p_ops[ i ] = NULL;
		}
		p_ops[ STREAM_OP_NUM - 1 ] = p_stream->p_ops[ STREAM_OP_NUM - 1 ];
		CALL_INTERFACE_FUNC( p_ops[ STREAM_OP_NUM - 1 ], gc_interface_s, ref_inc );
		break;
	case STREAM_INITIAL:
	case STREAM_RST_RECV:
	case STREAM_SHUT_RECV:
		close = 1;
		p_stream->status = STREAM_CLOSED;
		break;
	default:
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( close ){
		stream_close_func( p_stream );
	}else if( send_rst ){
		if( stream_send_rst( p_ops[ STREAM_OP_NUM - 1 ]
				, p_timers[ STREAM_RST_TIMER ], mark_id ) < 0 ){
			stream_remote_close( p_stream, 0 );
		}
		SESSION_FREE_PIPE( p_control ):
		stream_rshub_free( p_stream, recv_hub );
		stream_rshub_free( p_stream, send_hub );
		for( i = 0; i < STREAM_TIMER_NUM; i++ ){
			if( i != STREAM_RST_TIMER ){
				CALL_PIPE_POINT_FUNC( p_timers[ i ], timer_interface_s, stop );
			}
			CALL_PIPE_POINT_FUNC( p_timers[ i ], gc_interface_s, ref_dec );
		}
		for( i = 0; i < STREAM_OP_NUM -1; i++ ){
			SESSION_FREE_PIPE( p_ops[ i ] );
		}
		CALL_INTERFACE_FUNC( p_ops[ STREAM_OP_NUM - 1 ], gc_interface_s, ref_dec );
	}
}

static void stream_remote_close( stream p_stream, int error_code )
{
	rs_buf_hub_s send_hub;
	common_user_data p_timers[ STREAM_TIMER_NUM ];
	common_user_data p_ops[ STREAM_OP_NUM ], p_user_data;
	common_user_data p_control;
	common_user_data_u user_data;
	int closed, clear_bits, clear;

	clear_bits = close = clear = 0;
	pthread_mutex_lock( &p_stream->deferred_mutex );
	switch( p_stream->status ){
	case STREAM_SYN_RECV:
	case STREAM_SYN_SEND:
	case STREAM_SYNREPLY_RS:
		clear = 1;
		p_stream->status = STREAM_RST_RECV;
		clear_bits = p_stream->shut_flags & STREAM_CAN_SEND;
		if( p_stream->recv_hub.total_len == 0){
			clear_bits |= p_stream->shut_flags & STREAM_CAN_RECV;
			p_stream->status = STREAM_SHUT_RECV;
		}
		p_stream->shut_flags &= ~clear_bits;
		if( clear_bits != 0 ){
			p_control = p_stream->p_control;
			CALL_INTERFACE_FUNC( p_control, gc_interface_s, ref_inc );
		}
		send_hub = p_stream->send_hub;
		memset( &p_stream->send_hub, 0, sizeof( p_stream->send_hub ) );
		for( i = 0; i < STREAM_TIMER_NUM; i++ ){
			p_user_data = p_stream->p_timers[ i ];
			p_timers[ i ] = p_user_data;
			p_user_data[ 1 ].i32[ 1 ]++;
			CALL_INTERFACE_FUNC( p_user_data, gc_interface_s, ref_inc );
		}
		for( i = 0; i < STREAM_OP_NUM; i++ ){
			p_ops[ i ] = p_stream->p_ops[ i ];
			p_ops[ i ]->i32[ 1 ] = PIPE_STATUS_DEL;
			p_stream->p_ops[ i ] = NULL;
		}
		break;
	case STREAM_INITIAL:
	case STREAM_RST_SEND:
		close = 1;
		p_stream->status = STREAM_CLOSED;
		break;
	default:
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( close ){
		stream_close_func( p_stream );
	}else if( clear ){
		if( clear_bits & STREAM_CAN_RECV ){
			user_data.i_num = STREAM_EVENT_CLOSED;
			CALL_PIPE_POINT_FUNC( p_control, io_listener_interface_s, recv_event, user_data );
		}
		if( clear_bits & STREAM_CAN_SEND ){
			user_data.i_num = STREAM_EVENT_CLOSED;
			CALL_PIPE_POINT_FUNC( p_control, io_listener_interface_s, send_event, user_data );
		}
		if( clear_bits != 0 ){
			CALL_INTERFACE_FUNC( p_control, gc_interface_s, ref_dec );
		}
		stream_rshub_free( p_stream, send_hub );
		for( i = 0; i < STREAM_TIMER_NUM; i++ ){
			CALL_PIPE_POINT_FUNC( p_timers[ i ], timer_interface_s, stop );
			CALL_INTERFACE_FUNC( p_timers[ i ], gc_interface_s, ref_dec );
		}
		for( i = 0; i < STREAM_OP_NUM; i++ ){
			SESSION_FREE_PIPE( p_ops[ i ] );
		}
	}
}

static void	stream_timer_func( void * p_data, void * p_pipe, int mark_id )
{
	stream p_stream;
	common_user_data_u user_data;
	int clear_bits, remote_close;
	common_user_data p_user_data, p_control;

	p_stream = p_data;
	p_user_data = p_pipe;
	user_data.i_num = STREAM_EVENT_TIMEOUT;
	clear_bits = remote_close = 0;
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( !IS_STREAM_CLOSED( p_stream ) 
		&& p_user_data->i32[ 1 ] == PIPE_STATUS_ACTIVE 
		&& p_user_data[ 1 ].i32[ 1 ] == mark_id ){
		switch( p_user_data->i32[ 0 ] ){
		case STREAM_RECV_TIMER:
			clear_bits = p_stream->shut_flags & STREAM_CAN_RECV;
			break;
		case STREAM_SEND_TIMER:
			clear_bits = p_stream->shut_flags & STREAM_CAN_SEND;
			break;
		case STREAM_RST_TIMER:
			remote_close = 1;
			break;
		}
		if( clear_bits != 0 ){
			p_stream->shut_flags &= ~clear_bits;
			p_control = p_session->p_control;
			CALL_INTERFACE_FUNC( p_control, gc_interface_s, ref_inc );
		}
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( remote_close ){
		stream_remote_close( p_stream, 0 );
	}else if( clear_bits & STREAM_CAN_RECV ){
		CALL_PIPE_POINT_FUNC( p_control, io_listener_interface_s, recv_event, user_data );
		CALL_INTERFACE_FUNC( p_control, gc_interface_s, ref_dec );
	}else if( clear_bits & STREAM_CAN_SEND ){
		CALL_PIPE_POINT_FUNC( p_control, io_listener_interface_s, send_event, user_data );
		CALL_INTERFACE_FUNC( p_control, gc_interface_s, ref_dec );
	}
}

static void stream_free( stream p_stream )
{
	pthread_mutex_destroy( &p_stream->deferred_mutex );
	session_rsbuf_free( ( rs_buf )( ( char * )p_stream - p_stream->offset ) );
}

static void stream_close_func( stream p_stream )
{
	int i;
	void * p_pushed, * p_tmp;

	for( i = 0; i < STREAM_TIMER_NUM; i++ ){
		SESSION_FREE_PIPE( p_stream->p_timers[ i ] );
	}
	for( i = 0; i < STREAM_OP_NUM; i++ ){
		SESSION_FREE_PIPE( p_stream->p_ops[ i ] );
	}
	SESSION_FREE_PIPE( p_stream->p_session_pipe );
	SESSION_FREE_PIPE( p_stream->p_control );
	p_pushed = dlist_next( p_stream->p_pushed_head );
	while( p_pushed != NULL ){
		p_tmp = p_pushed;
		p_pushed = dlist_del( p_tmp );
		SESSION_FREE_PIPE( ( char * )p_tmp - STREAM_PIPE_LEN );
	}
	stream_rsblock_free( p_stream, p_stream->p_window_update );
	stream_rsblock_free( p_stream, p_stream->p_rst );
	stream_rshub_free( p_stream, &p_stream->recv_hub );
	stream_rshub_free( p_stream, &p_stream->send_hub );
}

static int stream_pipe_close( void * p_data, void * p_pipe )
{
	stream p_stream;
	int local_close, remote_close, free_pipe;
	common_user_data p_category;

	remote_close = local_close = free_pipe = 0;
	p_stream = p_data;
	p_category = p_pipe;
	pthread_mutex_lock( &p_session->op_mutex );
	if( !IS_STREAM_CLOSED( p_stream )
		&& p_category->i32[ 1 ] == PIPE_STATUS_ACTIVE ){
		p_category->i32[ 1 ] = PIPE_STATUS_STOP;
		switch( p_category->i32[ 0 ] ){
		case STREAM_CONTROL:
			local_close = 1;
			break;
		case STREAM_SESSION:
			local_close = remote_close = 1;
			break;
		case STREAM_ROOT:
			local_close = 1;
			break;
		case STREAM_PUSHED:
			free_pipe = 1;
			p_category->i32[ 1 ] = PIPE_STATUS_DEL;
			dlist_del( ( char * )p_category + STREAM_PIPE_LEN );
			break;
		case STREAM_RST_TIMER:
			remote_close = 1;
		case STREAM_RECV_TIMER:
		case STREAM_SEND_TIMER
			local_close = 1;
			break;
		case STREAM_RST_OP:
			remote_close = 1;
		case STREAM_OP:
		case STREAM_WIN_OP:
			local_close = 1;
			break;
		default:
			MOON_PRINT_MAN( ERROR, "unkonw stream pipe type!" );
		}
	}
	pthread_mutex_unlock( &p_session->op_mutex );
	if( remote_close ){
		stream_remote_close( p_stream, INTERNAL_ERROR );
	}
	if( local_close ){
		stream_local_close( p_stream, INTERNAL_ERROR );
	}
	if( free_pipe ){
		CALL_INTERFACE_FUNC( p_pipe, gc_interface_s, ref_dec );
	}
	return 0;
}

static void stream_pipe_data_free( void * p_pipe )
{
	common_user_data p_user_data;

	p_user_data = p_pipe;
	switch( p_user_data->i32[ 0 ] ){
	case STREAM_RECV_TIMER:
	case STREAM_SEND_TIMER:
	case STREAM_RST_TIMER:
	case STREAM_TIMER_NUM:
	case STREAM_SESSION:
	case STREAM_ROOT:
	case STREAM_OP:
	case STREAM_WIN_OP:
	case STREAM_RST_OP:
	case STREAM_WIN_OP:
		session_rsbuf_free( ( rs_buf )( ( char * )p_user_data - p_user_data[ 1 ].i32[ 0 ] ) );
		break;
	case STREAM_CONTROL:
		if( p_user_data[ 1 ].i32[ 0 ] > 0 ){
			session_rsbuf_free( ( rs_buf )( ( char * )p_user_data - p_user_data[ 1 ].i32[ 0 ] ) );
		}
	default:
	}
}

static void stream_get_pipe_data_len( void * p_data, int * p_len )
{
	if( p_len != NULL ){
		*p_len = STREAM_PIPE_LEN;
	}
}

static void session_ref_inc( void * p_data )
{
	GC_REF_INC( ( session )p_data );	
}

static inline void session_ref_dec( void * p_data )
{
	session p_session;
	int tmp;

	p_session = p_data;
	tmp = GC_REF_DEC( p_session );
	if( tmp == 0 ){
		if( !IS_SESSION_CLOSED( p_session ) ){
			MOON_PRINT_MAN( ERROR, "session not closed when free!" );
		}
		session_free( p_session );
	}else if( tmp < 0 ){
		MOON_PRINT_MAN( ERROR, "session ref num under overflow!" );
	}
}

static inline stream get_stream_ref( session p_session, int stream_id )
{
	char id_buf[ 32 ];
	stream p_stream = NULL;

	snprintf( id_buf, sizeof( id_buf ), "%d", stream_id );
	pthread_mutex_lock( &p_session->op_mutex );
	if( !IS_SESSION_CLOSED( p_session ) ){
		p_stream = hash_table_search( &p_session->stream_table, id_buf, 0 );
		if( p_stream != NULL ){
			stream_ref_inc( p_stream );
		}
	}
	pthread_mutex_unlock( &pp_session->op_mutex );
	return p_stream;
}

static void stream_ref_dec( void * p_data )
{
	int tmp;
	stream p_stream;

	p_stream = p_data;
	tmp = GC_REF_DEC( p_stream );
	if( tmp == 0 ){
		if( !IS_STREAM_CLOSED( p_stream ) ){
			MOON_PRINT_MAN( ERROR, "stream not closed when free!" );
		}
		stream_free( p_stream );
	}else if( tmp < 0 ){
		MOON_PRINT_MAN( ERROR, "stream ref num under overflow!" );
	}
}

static inline void stream_ref_inc( void * p_data )
{
	GC_REF_INC( ( stream )p_data );
}

/*frame head
big ---> little ( 32 bits )
flags( 8 bits ) | total length / other data( 24 bits )
types( 8 bits ) | stream id / other data( 24 bits )
*/

//flags
enum{

};

//types
enum{
	//belong to stream 1 - 196
	TYPE_SYN = 1,
	TYPE_SYN_REPLY = 2,
	TYPE_DATA = 3,
	TYPE_WINDOW_UPDATE = 4,
	TYPE_RST = 5,
	//belong to session 197 - 255
	TYPE_PING = 197,
	TYPE_GOAWAY = 198
};

enum{
	FRAME_HEAD_LEN = 8,
	FRAME_SYN_HEAD_LEN = 12,
	FRAME_RST_LEN = 8,
	FRAME_WINDOW_UPDATE_LEN = 8,
	FRAME_PING_LEN = 8,
	FRAME_GOAWAY_LEN = 8,
};

enum{
	PROTOCOL_ERROR = 0x1,//普通错误
	INVALID_STREAM,//非活动流收到数据
	REFUSED_STREAM,//无法创建新流或资源不足时
	UNSUPPORTED_VERSION，
	CANCEL,//流的创建者指定这个流不在需要
	INTERNAL_ERROR,//内部错误
	FLOW_CONTROL_ERROR,
	STREAM_IN_USE,//端点收到多余的SYN_REPLY
	STREAM_ALREADY_CLOSED,//端点关闭时，收到数据帧
	FRAME_TOO_LARGE
};

enum{
	PRI_BIT_LEN = 3,
	HIGHEST_PRI = 0x0,
	LOWEST_PRI = 0x7
};

#define GET_TYPE_OR_FLAGS( num ) ( ( uint32_t )( num ) >> 24 )
#define SET_TYPE_OR_FLAGS( num, type ) ( ( ( uint32_t )( num ) & 0xffffff )\
 | ( ( uint32_t )( type ) << 24 ) )
#define GET_LEN_OR_ID( num ) ( ( uint32_t )( num ) & 0xffffff )
#define SET_LEN_OR_ID( num, len ) ( ( ( uint32_t )( num ) & 0xff000000 ) \
 | ( ( uint32_t )( len ) & 0xffffff ) )
//syn frame
#define GET_SYN_PRI( num ) ( GET_TYPE_OR_FLAGS( num ) & LOWEST_PRI )
#define SET_SYN_PRI( num, pri ) SET_TYPE_OR_FLAGS( num, pri & LOWEST_PRI )

static inline void set_uint32( uint32_t ** pp_u32, int num )
{
	**pp_u32 = htobe32( num );
	( *pp_u32 )++;
}

static inline void set_frame_head( uint32_t ** pp_u32, int flags, int len, int type, int id )
{
	set_uint32( pp_u32, SET_LEN_OR_ID( SET_TYPE_OR_FLAGS( 0, flags ), len ) );
	set_uint32( pp_u32, SET_LEN_OR_ID( SET_TYPE_OR_FLAGS( 0, type ), id ) );
}

//frame head( length store status_code )
static inline void set_rst_frame( char * buf, int stream_id, int status_code )
{
	set_frame_head( &( uint32_t *)p_buf, 0, status_code, TYPE_RST, stream_id );
}

//frame head( id store ping_id )
static inline void set_ping_frame( char * p_buf, unsigned ping_id )
{
	set_frame_head( &( uint32_t *)p_buf, 0, FRAME_PING_LEN, TYPE_PING, ping_id );
}

//frame head( id store status_code )
static inline void set_goaway_frame( char * p_buf, int status_code )
{
	set_frame_control_head( &( uint32_t * )p_buf, 0, FRAME_GOAWAY_LEN, TYPE_GOAWAY, status_code );
}

//frame head( length store delta_window_size )
static inline void set_window_update_frame( char * p_buf, int stream_id, unsigned delta_window_size )
{
	set_frame_control_head( &( uint32_t * )p_buf, 0, delta_window_size, TYPE_WINDOW_UPDATE, stream_id );
}

//frame head + [ unused( 5 bits ) + pri( 3 bits ) + assoc_id( 24 bits ) ]
static inline void set_syn_head_frame( char * p_buf, int stream_id, int flags, int data_len, int assoc_id, int pri )
{
	uint32_t * p_u32;

	p_u32 = ( uint32_t * )p_buf;
	set_frame_head( &p_u32, flags, FRAME_SYN_HEAD_LEN + data_len, TYPE_SYN, stream_id );
	set_uint32( &p_u32, SET_LEN_OR_ID( SET_SYN_PRI( 0, pri ), assoc_id ) );
}

//frame head
static inline void set_synreply_or_data_head_frame( char * p_buf, int stream_id, int flags, int data_len, int type )
{
	set_frame_head( &( uint32_t * )p_u32, flags, FRAME_HEAD_LEN + data_len, type, stream_id );
}

static int pre_process_packet( output_packet p_packet )
{
	stream p_stream;
	session p_session;
	int ret, closed, can_send;

	ret = -1;
	closed = can_send = 0;
	switch( p_packet->type ){
	case 0://data
	case TYPE_SYN:
	case TYPE_SYN_REPLY:
		p_stream = p_packet->p_stream;
		pthread_mutex_lock( &p_stream->deferred_mutex );
		if( p_stream->status == STREAM_CLOSING 
			|| ( p_stream & STREAM_INTERNAL_CLOSED ) != 0 ){
			ret = -1;		
		}else{
			ret = 0;
			p_stream->send_buf_size += p_packet->user_data.i_num;
			if( ( can_send = STREAM_CAN_SEND( p_stream ) ) != 0 ){
				p_stream->status |= STREAM_SENDING;
			}
			if( ( p_stream->shut_flags & STREAM_USER_CLOSED ) != 0 
				&& p_stream->send_buf_size == p_stream->send_buf_limit ){
				p_stream->status = STREAM_CLOSING;
				p_session = p_stream->p_session;
				session_ref_inc( p_session );
				closed = 1;
			}
		}
		pthread_mutex_unlock( &p_stream->deferred_mutex );
		if( can_send > 0 ){
			stream_start_send_task( p_stream );
		}
		if( closed != 0 ){
			stream_close_func( p_stream );
			add_rst_packet_to_session( p_session, p_stream->stream_id, p_stream->pri, 0 );
			session_ref_dec( p_session );
		}
		stream_ref_dec( p_stream );
		break;
	case TYPE_WINDOW_UPDATE:
		ret = 0;
		p_stream = p_packet->p_stream;
		pthread_mutex_lock( &p_stream->deferred_mutex );
		p_stream->recv_window_size += p_packet->user_data.i_num;
		pthread_mutex_unlock( &p_stream->deferred_mutex );
		stream_ref_dec( p_stream );
		break;
	case TYPE_RST:
		ret = 0;
		//do nothing
		break;
	default:
		MOON_PRINT_MAN( ERROR, "illegal packet type!" );
	}
	return ret;
}

static int add_session_timer( session p_session, int category, timer_desc p_desc )
{
	void * p_point, * p_point_data;
	int ret, mark_id;
	common_user_data p_user_data;

	ret = -1;
	can_add = 0;
	pthread_mutex_lock( &p_session->op_mutex );
	if( !IS_SESSION_CLOSED( p_session ) 
		&& p_session->p_timers[ category ] != NULL ){
		p_user_data = p_session->p_timers[ category ];
		p_user_data[ 1 ].i_num++;
		mark_id = p_user_data[ 1 ].i_num;
		CALL_INTERFACE_FUNC( p_user_data, gc_interface_s, ref_inc );
		can_add = 1;
	}
	pthread_mutex_unlock( &p_session->op_mutex );
	if( can_add ){
		p_desc->mark_id = mark_id;
		CALL_PIPE_POINT_FUNC_RET( ret, p_user_data, timer_interface_s, set, &desc );
		CALL_INTERFACE_FUNC( p_user_data, gc_interface_s, ref_dec );
	}
	return ret; 
}

static inline int try_start_send_task( session p_session )
{
	if( ( p_session->status & ( SESSION_SENDING | SESSION_CAN_SEND ) ) == SESSION_CAN_SEND
		&& p_session->packet_num > 0 ){
		p_session->status &= ~SESSION_CAN_SEND;
		p_session->status |= SESSION_SENDING;
		return 1;
	}
	return 0;
}

static inline int start_rs_task( session p_session, int type )
{
	process_task_s task;
	void * p_ppool;
	pthreadpool_interface p_ppool_i;
	
	p_ppool = get_pthreadpool_instance();
	if( __builtin_expect( p_ppool == NULL, 0 ) ){
		MOON_PRINT_MAN( ERROR, "pthread pool error!" );
		return -1;
	}
	p_ppool_i = FIND_INTERFACE( p_ppool, pthreadpool_interface_s );
	session_ref_inc( p_session );
	task.task_func = type == SESSION_RECVING ? session_recv : session_send;
	task.user_data[ 0 ].ptr = p_session;
	if( __builtin_expect( p_ppool_i->put_task( p_ppool, &task ) < 0, 0 ) ){
		MOON_PRINT_MAN( ERROR, "put rs task error!" );
		session_ref_dec( p_session );
		return -1;
	}
	return 0;
}

static inline int start_rs_task_with_timer( session p_session, int type )
{
	timer_desc_s desc;
	int cate;

	if( start_rs_task( p_session, type ) < 0 ){
		//加一个定时器任务，隔一段时间后再试
		desc.time_us = RS_RETRY_MS * 1000;
		desc.repeat_num = 1;
		cate = type == SESSION_RECVING ? SESSION_RECV_TIMER: SESSION_SEND_TIMER;
		if( add_session_timer( p_session, cate, &desc ) < 0 ){
			MOON_PRINT_MAN( ERROR, "add rs timer error!" );
			return -1;
		}
	}
	return 0;
}

static void	session_timer_func( void * p_data, void * p_pipe, int mark_id )
{
	session p_session;
	common_user_data p_user_data;
	int type;

	p_session = p_data;
	p_user_data = p_pipe;
	type = p_user_data->i_num == SESSION_RECV_TIMER ? SESSION_RECVING : SESSION_SENDING;
	if( start_rs_task_with_timer( p_session, type ) < 0 ){
		session_triger_free( p_session, SESSION_GOAWAY_RECV, 0 );
	}
}

static int add_packet_to_session( session p_session, output_packet p_out_packet, int is_syn )
{
	int ret, can_send;
	
	if( p_session->status & SESSION_GOAWAY_RECV ){
		MOON_PRINT_MAN( ERROR, "session already cloae!" );
		return -1;
	}
	if( p_out_packet->seq == 0 ){
		p_out_packet->seq =	__sync_fetch_and_add( &p_session->next_seq, 1 );
	}
	pthread_mutex_lock( &p_session->op_mutex );
	if( is_syn ){
		ret = heap_push( p_session->op_heap, p_out_packet );
	}else{
		ret = heap_push( p_session->op_ss_heap, p_out_packet );
	}
	if( ret < 0 ){
		MOON_PRINT_MAN( ERROR, "add to op heap error!" );
		pthread_mutex_unlock( &p_session->op_mutex );
		return ret;
	}
	can_send = try_start_send_task( p_session );
	pthread_mutex_unlock( &p_session->op_mutex);
	if( can_send ){
		start_rs_task( p_session, SESSION_SENDING );
	}
	return 0;
}

static inline int add_winupdate_packet_to_session( session p_session, stream p_stream, int size )
{
	output_packet_s packet;
	int ret;
	ret = -1;

	packet.p_buf_head = create_window_update_frame( p_stream, size );
	if( p_buf_head != NULL ){
		packet.pri = p_stream->pri;
		packet.type = TYPE_WINDOW_UPDATE;
		packet.user_data.i_num = size;
		stream_ref_inc( p_stream );
		packet.p_stream = p_stream;
		if( add_packet_to_session( p_session, p_buf_head, 0 ) < 0 ){
			free_total_packet( packet.p_buf_head );
			stream_ref_dec( packet.p_stream );
		}else{
			ret = 0;
		}
	}
	return ret;
}

static int try_send_packet( stream p_stream, output_packet p_packet )
{
	output_packet_s packet, split_packet;
	buf_head p_split_buf1, p_split_buf2;
	int ret, len, split_len, assoc_id;
	buf_desc_s buf_desc;
	
	ret = 0;
	if( heap_top( p_stream->p_deferred_op, &packet ) >= 0 ){
		len = get_packet_len( packet.p_buf_head );
		if( len <= p_stream->window_size 
			|| ( p_stream->window_size >= MIN_SEND_WINDOW_SIZE && len > MIN_PACKET_SPLIT_LEN ) ){
			heap_pop( p_stream->p_deferred_op, &packet );
			split_len = MIN( p_stream->window_size, MIN_PACKET_SPLIT_LEN );
			if( len > split_len ){
				begin_split_packet( packet.p_buf_head, 0 );
				if( get_next_packet( packet.p_buf_head, split_len, &p_split_buf1 ) <= 0 ){
					MOON_PRINT_MAN( ERROR, "split packet error!" );
					goto error;
				}
				if( get_next_packet( packet.p_buf_head, 0, &p_split_buf2 ) <= 0 ){
					MOON_PRINT_MAN( ERROR, "split packet error!" );
					free_total_packet( p_split_buf1 );
					goto error;
				}
				free_total_packet( packet.p_buf_head );
				packet.p_buf_head = p_split_buf1;
				split_packet = packet;
				split_packet.p_buf_head = p_split_buf2;
				split_packet.split_index++;
				if( heap_push( p_stream->p_deferred_op, &split_packet ) < 0 ){
					MOON_PRINT_MAN( ERROR, "push packet error!" );
					free_total_packet( split_packet.p_buf_head );
					goto error;
				}
				len = split_len;
			}
			p_stream->window_size -= len;
			packet.user_data.i_num = len;
			if( p_stream->status == STREAM_INITIAL ){
				p_stream->status = STREAM_SYN_RS;
				packet.type = TYPE_SYN;
				assoc_id = 0;
				if( p_stream->is_pushed != 0 ){
					assoc_id = p_stream->p_main_stream->stream_id;
				}
				buf_desc.offset = 0;
				buf_desc.len = FRAME_SYN_HEAD_LEN;
				buf_desc.buf = malloc_syn_head_frame( p_stream->stream_id, 0, len, assoc_id, p_stream->pri )
			}else if( p_stream->status == STREAM_SYN_RS 
			&& ( p_stream->stream_id % 2 ) == p_stream->p_session->is_server ){
				p_stream->status = STREAM_SYNREPLY_RS;
				packet.type = TYPE_SYN_REPLY;
				buf_desc.offset = 0;
				buf_desc.len = FRAME_HEAD_LEN;
				buf_desc.buf = malloc_synreply_or_data_head_frame( p_stream->stream_id, 0, len, TYPE_SYN_REPLY );
			}else{
				packet.type = 0;
				buf_desc.offset = 0;
				buf_desc.len = FRAME_HEAD_LEN;
				buf_desc.offset = 0;
				buf_desc.len = FRAME_HEAD_LEN;
				buf_desc.buf = malloc_synreply_or_data_head_frame( p_stream->stream_id, 0, len, TYPE_DATA );
			}
			if( buf_desc.buf == NULL ){
				MOON_PRINT_MAN( ERROR, "malloc packet head error!" );
				goto error;
			}
			if( add_buf_to_packet( &packet.p_buf_head, &buf_desc ) < 0 ){
				MOON_PRINT_MAN( ERROR, "add head error!" );
				goto add_error;
			}
			packet.p_stream = p_stream;
			stream_ref_inc( p_stream );
			*p_packet = packet;
			ret = 1;
		}
	}
	goto back;

add_error:
	packet_buf_free( buf_desc.buf );
error:
	free_total_packet( packet.p_buf_head );
	ret = -1;
back:
	return ret;
}

static inline int loop_send_packet( stream p_stream, unsigned delta_window_size )
{
	session p_session;
	output_packet_s packet;
	int can_send, ret, frist;

	first = STREAM_SEND_TO_SESSION;
	for( ; ; ){
		can_send = 0;
		pthread_mutex_lock( &p_stream->deferred_mutex );
		p_stream->window_size += delta_window_size;
		if( p_stream->status != STREAM_CLOSING 
			&& ( p_stream->shut_flags & STREAM_INTERNAL_CLOSED ) == 0 
			&& ( ( p_stream->shut_flags & STREAM_SEND_TO_SESSION ) ^ first ) != 0 ){
			p_stream->shut_flags |= first;
			first ^= first;
			can_send = try_send_packet( p_stream, &packet );
			if( can_send > 0 ){//阻止其他线程发
				delta_window_size = p_stream->window_size;
				p_stream->window_size = 0;
				p_session = p_stream->p_session;
				session_ref_inc( p_session );
			}else{
				p_stream->shut_flags &= ~STREAM_SEND_TO_SESSION;
			}
		}
		pthread_mutex_unlock( &p_stream->deferred_mutex );
		if( can_send > 0 ){
			ret = add_packet_to_session( p_session, &packet, packet.type == TYPE_SYN );
			session_ref_dec( p_session );
			if( ret >= 0 ){
				continue;
			}
			free_total_packet( packet.p_buf_head );
			stream_ref_dec( packet.p_stream );
			can_send = -1;
		}
		break;
	}
	if( can_send < 0 ){
		inter_error_stream( p_stream, INTERNAL_ERROR );
	}
	return can_send;
}

#define SESSION_FREE_PIPE( p_pipe ) do{\
	typeof( p_pipe ) _p_pipe;\
	\
	if( ( _p_pipe = p_pipe ) != NULL ){\
		CALL_INTERFACE_FUNC( _p_pipe, pipe_interface_s, close ):\
		CALL_INTERFACE_FUNC( _p_pipe, gc_interface_s, ref_dec );\
	}\
}while( 0 )

static int _del_streams( void * ptr, common_user_data_u user_data )
{
	common_user_data p_user_data;

	p_user_data = ptr;
	p_user_data = ( common_user_data )( ( char * )( p_user_data + 1 ) - SESSION_TO_STREAM_LEN );
	SESSION_FREE_PIPE( p_user_data );
	return 0;
}

static void session_close_func( session p_session )
{
	common_user_data_u user_data;
	void * p_tmp, * p_next;
	int i;

	//control
	SESSION_FREE_PIPE( p_session->p_control );
	//io
	SESSION_FREE_PIPE( p_session->p_io );
	//ping
	SESSION_FREE_PIPE( p_session->p_ping );
	//timers
	for( i = 0; i < SESSION_TIMER_NUM; i++ ){
		SESSION_FREE_PIPE( p_session->p_timers[ i ] );
	}
	//new_streams
	p_next = dlist_next( p_session->p_listener_head );
	while( p_next != NULL ){
		p_tmp = p_next;
		p_next = dlist_del( p_tmp );
		SESSION_FREE_PIPE( ( ( common_user_data )data_to_list( p_tmp ) ) - 1 );
	}
	//aop
	reset_aiop( &p_session->aop );
	//aip
	reset_aiop( &p_session->aip );
	//streams
	avl_traver_lastorder( p_session->p_streams_avl, _del_streams, user_data );
	//packets
	avl_traver_lastorder( p_session->p_ops_avl, _del_streams, user_data );
	//op
	SESSION_FREE_PIPE( p_session->p_op );
	//rsts
	for( i = 0; i < 2; i++ ){
		session_rsblock_free( p_session->p_rsts[ i ] );
	}
}

static inline void session_triger_free( session p_session, int set_bits, int useing )
{
	int is_free = 0;

	pthread_mutex_lock( &p_session->op_mutex );
	if( !IS_SESSION_FREED( p_session ) ){
		p_session->status |= set_bits;
		p_session->status -= useing;
		is_free = IS_SESSION_FREED( p_session );
	}
	pthread_mutex_unlock( &p_session->op_mutex );
	if( is_free ){
		session_close_func( p_session );
	}
}

static inline void close_session( session p_session )
{
	session_triger_free( p_session, SESSION_GOAWAY_RECV, 0 );
}

static inline rs_block cut_packet_head( rs_block p_block, int head_len )
{
	rs_block_s prev_block;

	prev_block = *p_block;
	p_block = ( rs_block )( ( char * )p_block + head_len );
	prev_block.offset += head_len;
	prev_block.len -= head_len;
	*p_block = prev_block;
	return p_block;
}

static int end_process_recv_packet( session p_session, active_packet p_aip )
{
	rs_block p_block, p_tmp;
	uint32_t * p_u32, u32;
	stream p_stream, p_assoc_stream;
	int ret, ch_status;
	int type, len, flags, stream_id, assoc_id, pri, head_len;

	ret = -1;
	ch_status = 0;
	p_stream = p_aip->p_stream;
	p_block = p_aip->p_block;
	p_aip->p_block = NULL;
	p_u32 = ( uint32_t * )( p_block + 1 );
	u32 = be32toh( *p_u32 );
	flags = GET_TYPE_OR_FLAGS( u32 );
	len = GET_LEN_OR_ID( u32 );
	p_u32++;
	u32 = be32toh( *p_u32 );
	type = GET_TYPE_OR_FLAGS( u32 );
	stream_id = GET_LEN_OR_ID( u32 );
	switch( type ){
	case TYPE_SYN:
		p_u32++;
		u32 = be32toh( *p_u32 );
		assoc_id = GET_LEN_OR_ID( u32 );
		pri = GET_SYN_PRI( u32 );
		p_session->last_recv_stream_id = stream_id;
		p_stream = p_assoc_stream = NULL;
		if( assoc_id != 0 ){
			if( ( assoc_id % 2 ) != p_session->is_server 
				&& ( p_assoc_stream = get_stream_ref( p_session, assoc_id ) ) != NULL 
				&& p_assoc_stream->is_pushed == 0 
				&& p_assoc_stream->status > STREAM_INITIAL ){
				p_stream = stream_new( p_session, p_assoc_stream, stream_id, pri );
			}
			if( p_assoc_stream != NULL ){
				stream_ref_dec( p_assoc_strea );
			}
		}else{//normal stream
			p_stream = stream_new( p_session, NULL, stream_id, pri );
		}
		if( p_stream != NULL){
			p_aip->p_stream = p_stream;
			__sync_fetch_and_add( &p_stream->mem_used, get_rs_block_size( p_block ) );
		}else{
			MOON_PRINT_MAN( ERROR, "new stream error!" );
			add_rst_packet_to_session( p_session, stream_id, PROTOCOL_ERROR, USER_PRI1 );
			goto back;
		}
		head_len = FRAME_SYN_HEAD_LEN;
		break;
	case TYPE_SYN_REPLY:
		ch_status = STREAM_SYNREPLY_RS - STREAM_SYN_SEND;
		head_len = FRAME_HEAD_LEN;
		break;
	case TYPE_DATA:
		head_len = FRAME_HEAD_LEN;
		break;
	}
	cut_packet_head( p_block, head_len );
	p_block->flags |= IS_READY;
	ret = inter_add_packet_to_stream( p_stream, p_block, len - head_len, ch_status );
	if( ret != 0 ){
		p_aip->p_block = p_block;
		inter_error_stream( p_stream, INTERNAL_ERROR );
	}
back:
	return ret;
}

static void * stream_rsbuf_malloc( stream p_stream, uint64_t min, uint64_t max, uint64_t * p_len )
{
	rs_buf p_buf;
	uint64_t tmp_len;

	min += sizeof( *p_buf );
	max += sizeof( *p_buf );
	if( stream_get_mem( p_stream, min, max, &tmp_len ) == 0 ){
		if( ( p_buf = malloc( tmp_len ) ) != NULL ){
			p_buf->len = tmp_len;
			p_buf->ref_num = 1;
			*p_len = tmp_len - sizeof( *p_buf );
			return p_buf + 1;
		}else{
			stream_put_mem( p_stream, tmp_len );
		}
	}
	return NULL;
}

static void * session_rs_buf_malloc( uint64_t min, uint64_t max, uint64_t * p_len )
{
	rs_buf p_buf;
	uint64_t tmp_len;

	min += sizeof( *p_buf );
	max += sizeof( *p_buf );
	if( get_mem( 1, min, max, &tmp_len ) == 0 ){
		if( ( p_buf = malloc( tmp_len ) ) != NULL ){
			p_buf->len = tmp_len;
			p_buf->ref_num = 1;
			*p_len = tmp_len - sizeof( *p_buf );
			return p_buf + 1;
		}else{
			put_mem( tmp_len );
		}
	}
	return NULL;
}

static void stream_rsblock_free( stream p_stream, rs_block p_block )
{
	int len;

	if( ( len = rs_block_ref_dec( p_block ) ) > 0 ){
		stream_put_mem( p_stream, len );
	}
}

static void session_rsblock_free( rs_block p_block )
{
	int len;

	if( ( len = rs_block_ref_dec( p_block ) ) > 0 ){
		put_mem( len );
	}
}

static void stream_rsbuf_free( stream p_stream, rs_buf p_buf )
{
	int len;

	if( ( len = rs_buf_ref_dec( p_buf ) ) > 0 ){
		stream_put_mem( p_stream, len );
	}
}

static void session_rs_buf_free( rs_buf p_buf )
{
	int len;

	if( ( len = rs_buf_ref_dec( p_buf ) ) > 0 ){
		put_mem( len );
	}
}

static void init_rs_block( rs_block p_block, int len )
{
	p_block->flags = 0;
	p_block->len = len - sizeof( *p_block );
	p_block->offset = sizeof( rs_buf_s );
	p_block->next = NULL;
}

static rs_block alloc_recv_packet( stream p_stream, int len, int head_len, int piece_len )
{
	rs_block p_block, p_head, p_tail, p_block_free;
	rs_buf_hub p_hub;
	uint64_t tmp_len;
	int alloc_len, fetch_num;

	alloc_len = 0;
	p_block_free = p_block = NULL;
	p_hub = &p_stream->recv_hub;
	if( len >= BUF_BASE_ACTUAL_SIZE ){
		alloc_len = len + sizeof( rs_block_s );
	}else if( p_stream == NULL 
		|| len > p_hub->common_buf_left ){//dont need lock
		alloc_len = BUF_BASE_ACTUAL_SIZE + sizeof( rs_block_s );
	}
	if( alloc_len > 0 ){
		if( p_stream != NULL ){
			p_block = stream_rsbuf_malloc( p_stream, alloc_len, alloc_len, &tmp_len );
		}else{
			p_block = session_rsbuf_malloc( alloc_len, alloc_len, &tmp_len );
		}
		if( p_block != NULL ){
			init_rs_block( p_block, tmp_len );
			if( p_block->len >= len ){
				p_block->flags = IS_HEAD | IS_TAIL;
				return p_block;
			}
		}else{
			MOON_PRINT_MAN( ERROR, "malloc error!" );
			return NULL;
		}
	}
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( CAN_STREAM_ADD_BUF( p_stream ) ){
		if( p_hub->p_common_buf != NULL ){
			p_hub->p_common_buf->next = p_block;
		}else{
			p_hub->p_common_buf = p_block;
			p_hub->common_buf_left = p_block->len;
			p_block->len = 0;
		}
		fetch_num = 1;
		p_head = get_rs_block( p_hub, FRAME_SYN_HEAD_LEN, &fetch_num, 0, &p_block_free );
		p_tail = p_head;
		fetch_num = len - FRAME_SYN_HEAD_LEN;
		while( fetch_num != 0 ){
			p_block = get_rs_block( p_hub, 1, &fetch_num, 1, &p_block_free );
			if( p_block != p_tail ){
				p_tail->next = p_block;
				p_tail = p_block;
			}
		}
		p_head->flags |= IS_HEAD;
		p_tail->flags |= IS_TAIL;
		move_common_buf( p_hub, FRAME_HEAD_LEN );
	}else{
		p_block_free = p_block;
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	while( p_block_free != NULL ){
		stream_rsblock_free( p_block_free );
	}
	return p_head;
}

enum{
	SESSION_RECV_ERROR = 1,
	SESSION_RECV_DROP,
	SESSION_RECV_CONTINUE,
	SESSION_RECV_NEXT_PACKET,
	SESSION_RECV_TRY_AGAIN
} session_recv_ret_e;

//ret:session_recv_ret_e
//根据头部进行过滤
static int pre_process_recv_packet( session p_session, active_input_packet p_aip )
{
	uint32_t u32;
	stream p_stream;
	int type, len, stream_id, ret, ping_id;
	void * p_ping;
	pipe_interface p_pipe_i;

	u32 = be32toh( *( uint32_t * )p_aip->headbuf );
	len = GET_LEN_OR_ID( u32 );
	u32 = be32toh( *( ( uint32_t * )p_aip->headbuf + 1 ) );
	type = GET_TYPE_OR_FLAGS( u32  );
	stream_id = GET_LEN_OR_ID( u32 );
	ret = SESSION_RECV_NEXT_PACKET;
	switch( type ){
	//TYPE_SYN, TYPE_SYN_REPLY, TYPE_DATA不允许空头
	case TYPE_SYN:
		if( len > FRAME_SYN_HEAD_LEN
			&& len <= FRAME_SIZE_MAX
			&& ( stream_id % 2 ) == p_session->is_server 
			&& stream_id > p_session->last_recv_stream_id ){
			p_aip->p_block = alloc_recv_packet( NULL, len, FRAME_SYN_HEAD_LEN, 1 );
			if( p_aip->p_block != NULL ){
				p_aip->p_stream = NULL;
				ret = SESSION_RECV_CONTINUE;
			}else{
				ret = SESSION_RECV_TRY_AGAIN;
			}
		}else if( ( p_stream = get_stream_ref( p_session, stream_id ) ) != NULL ){
			MOON_PRINT_MAN( ERROR, "syn frame: stream already exist!" );
			inter_error_stream( p_stream, STREAM_IN_USE );
			stream_ref_dec( p_stream );
		}else{
			MOON_PRINT_MAN( ERROR, "syn frame: error" );
			add_rst_packet_to_session( p_session, stream_id, PROTOCOL_ERROR, USER_PRI1 );
		}
		if( ret == SESSION_RECV_CONTINUE && len > FRAME_HEAD_LEN ){
			ret = SESSION_RECV_DROP;
			p_aip->buf_len = len - FRAME_HEAD_LEN;
		}
		break;
	case TYPE_SYN_REPLY:
	case TYPE_DATA:
		if( ( p_stream = get_stream_ref( p_session, stream_id ) ) != NULL ){
			if( len > FRAME_HEAD_LEN 
				&& len <= FRAME_SIZE_MAX
				&& len <= p_stream->recv_window_size + FRAME_HEAD_LEN 
				&& (	type == TYPE_SYN_REPLY 
						&& p_stream->status == STREAM_SYN_SEND 
						&& p_stream->is_pushed == 0
					|| type == TYPE_DATA 
						&& ( p_stream->status == STREAM_SYN_RECV 
							|| p_stream->status == STREAM_SYNREPLY_RS ) ) ){
				p_aip->p_block = alloc_recv_packet( p_stream, len, FRAME_HEAD_LEN, 1 );
				if( p_aip->p_block != NULL ){
					stream_ref_inc( p_stream );
					p_aip->p_stream = p_stream;
					ret = SESSION_RECV_CONTINUE;
				}else{
					ret = SESSION_RECV_TRY_AGAIN;
				}
			}else{
				MOON_PRINT_MAN( ERROR, "syn_reply frame: stream status error!" );
				inter_error_stream( p_stream, PROTOCOL_ERROR );
			}
			stream_ref_dec( p_stream );
		}else{
			MOON_PRINT_MAN( ERROR, "syn_reply frame: error" );
			add_rst_packet_to_session( p_session, stream_id, PROTOCOL_ERROR, USER_PRI1 );
		}
		if( ret == SESSION_RECV_CONTINUE && len > FRAME_HEAD_LEN ){
			ret = SESSION_RECV_DROP;
			p_aip->buf_len = len - FRAME_HEAD_LEN;
		}
		break;
	case TYPE_WINDOW_UPDATE:
		if( ( p_stream = get_stream_ref( stream_id ) != NULL ) ){
			loop_send_packet( p_stream, len );
		}else{
			MOON_PRINT_MAN( ERROR, "window update:can't find stream" );
			add_rst_packet_to_session( p_session, stream_id, PROTOCOL_ERROR, USER_PRI1 );
		}
		break;
	case TYPE_RST:
		if( ( p_stream = get_stream_ref( stream_id ) != NULL ) ){
			MOON_PRINT_MAN( NORMAL, "stream %d is closed %d", stream_id, len );
			inter_close_stream( p_stream, len );
		}
		break;
	case TYPE_PING:
		ping_id = stream_id;
		if( ( ping_id % 2 ) == p_session->is_server ){//ping syn,just ack
			add_ping_packet_to_session( p_session, ping_id );
		}else{//ping ack
			p_ping = NULL;
			pthread_mutex_lock( &p_session->op_mutex );
			if( !IS_SESSION_CLOSED( p_session )
				&& ping_id == p_session->last_ping_unique_id ){
				p_ping = p_session->p_ping;
				p_session->p_ping = NULL;
			}
			pthread_mutex_unlock( &p_session->op_mutex );
			if( p_ping != NULL ){
				p_pipe_i = CALL_PIPE_POINT_FUNC( p_ping
					, order_listener_interface_s, on_ready, NULL );
				p_pipe_i->close( p_ping );
				CALL_INTERFACE_FUNC( p_ping, gc_interface_s, ref_dec );
			}
		}
		break;
	case TYPE_GOAWAY:
		ret = SESSIOM_RECV_ERROR;
		close_session( p_session );
		break;
	default:
		MOON_PRINT_MAN( ERROR, "unknow frame type:%d", type );
		ret = SESSIOM_RECV_ERROR;
		close_session( p_session );
	}
	return ret;
}

static inline void reset_aiop( active_packet p_aiop )
{
	rs_block p_block;

	p_block = p_aiop->p_block;
	if( p_block != NULL ){
		if( p_aiop->p_stream != NULL ){
			stream_rsblock_free( p_aiop->p_stream, p_block );
		}else{
			session_rsblock_free( p_block );
		}
		p_aiop->p_block = NULL;
	}
	if( p_aiop->p_stream != NULL ){
		stream_ref_dec( p_stream );
		p_aiop->p_stream = NULL;
	}
	p_aiop->cur_buf_index = 0;
	p_aiop->buf_len = 0;
	p_aiop->buf = NULL;
	p_aiop->is_drop = 0;
}

static inline void init_aip( active_packet p_aip )
{
	p_aip->cur_buf_index = 0;
	p_aip->buf = p_aip->headbuf;
	p_aip->buf_len = sizeof( p_aip->headbuf );
}

static inline int session_rs_continue( p_session, status_bit, test_bit )
{
	int again = 1;
	
	pthread_mutex_lock( &p_session->op_mutex );
	again ^= ( ( p_session )->status & SESSION_GOAWAY_RECV ) / SESSION_GOAWAY_RECV;\
	again &= ( ( p_session )->status & ( test_bit ) ) / ( test_bit );\
	( p_session )->status &= ~( ( status_bit ) | ( test_bit ) );\
	( p_session )->status |= ( status_bit ) * again;\
	pthread_mutex_unlock( &p_session->op_mutex );
	return again;
}

//在线程里独立运行
static int session_recv( common_user_data p_user_data )
{
	session p_session;
	active_packet p_aip;
	int ret, need_read, clear_bits, set_bits;
	int useing;
	char * p_read;
	rs_block p_block;
	char tmp_buf[ FRAME_DROP_BUFFER_SIZE ];
	void * p_io, * p_point, * p_point_data;
	io_pipe_interface p_io_i;
	pipe_interface_s p_pipe_i;
	timer_desc_s desc;

	useing = set_bits = 0;
	p_io = NULL;
	p_session = ( session )p_user_data->ptr;
	p_aip = &p_session->aip;
	pthread_mutex_lock( &p_session->op_mutex );
	if( !IS_SESSION_CLOSED( p_session ) ){
		p_io = p_session->p_io;
		if( p_io != NULL ){
			useing = SESSION_USEING_UNIT;
			p_session->status += useing;
			CALL_INTERFACE_FUNC( p_io, gc_interface_s, ref_inc );
		}
	}
	pthread_mutex_unlock( &p_session->op_mutex );
	if( __builtin_expect( p_io == NULL, 0 ) ){
		goto back;
	}
	p_pipe_i = FIND_INTERFACE( p_io, pipe_interface_s );
	ret = p_pipe_i->get_other_point_ref( p_io, &p_point, &p_point_data );
	if( __builtin_expect( ret < 0 , 0 ) ){
		goto back1;
	}
	p_io_i = FIND_INTERFACE( p_point, io_pipe_interface_s );
	while( !IS_SESSION_CLOSED( p_session ) ){
		need_read = p_aip->buf_len - p_aip->cur_buf_index;
		if( need_read > 0 ){
			if( p_aip->is_drop == 0 ){
				p_read = p_aip->buf + p_aip->cur_buf_index;
			}else{
				p_read = tmp_buf;
				need_read = MIN( need_read, FRAME_DROP_BUFFER_SIZE );
			}
			ret = p_io_i->recv( p_point, p_point_data, p_read, need_read, 0 );
			if( ret > 0 ){
				p_aip->cur_buf_index += ret;
			}
			if( ret == SOCKET_NO_RES || ( ret > 0 && ret < need_read ) ){
				if( session_rs_continue( p_session
						, SESSION_RECVING, SESSION_CAN_RECV ) > 0 ){
					continue;
				}
				break;
			}else if( ret < 0 ){
				MOON_PRINT_MAN( ERROR, "recv error!" );
				set_bits = SESSION_GOAWAY_RECV;
				break;
			}
		}else{
			if( p_aip->buf == p_aip->headbuf ){
				ret = pre_process_recv_packet( p_session, p_aip );
				switch( ret ){
				case SESSION_RECV_CONTINUE:
					p_block = p_aip->p_block;
					p_aip->buf = ( char * )( p_block + 1 );
					memcpy( p_aip->buf, p_aip->headbuf, FRAME_HEAD_LEN );
					p_aip->cur_buf_index = FRAME_HEAD_LEN;
					p_aip->buf_len = p_block->len;
					break;
				case SESSION_RECV_NEXT_PACKET:
					init_aip( p_aip );
					break;
				case SESSION_RECV_DROP:
					p_aip->cur_buf_index = 0;
					p_aip->is_drop = 1;
					break;
				case SESSION_RECV_TRY_AGAIN:
					desc.time_us = RS_RETRY_MS * 1000;
					desc.num = 1;
					if( add_session_timer( p_session, SESSION_RECV_TIMER, &desc ) < 0 ){
						MOON_PRINT_MAN( ERROR, "add recv timer error!" );
						set_bits = SESSION_GOAWAY_RECV;
					}
					goto back2;
				case SESSIOM_RECV_ERROR:
					set_bits = SESSION_GOAWAY_RECV;
					goto back2;
				}
			}else if( p_aip->p_block != NULL ){
				p_block = ( ( rs_block )p_aip->buf -1 )->next;
				if( p_block != NULL ){
					p_aip->buf = ( char * )( p_block + 1 );
					p_aip->cur_buf_index = 0;
					p_aip->buf_len = p_block->len;
				}else{
					end_process_recv_packet( p_aip );
					reset_aiop( p_aip );
					init_aip( p_aip );
				}
			}else{
				init_aip( p_aip );
			}
		}
	}
back2:
	CALL_INTERFACE_FUNC( p_point, gc_interface_s, ref_dec );
	CALL_INTERFACE_FUNC( p_point_data, gc_interface_s, ref_dec );
back1:
	CALL_INTERFACE_FUNC( p_io, gc_interface_s, ref_dec );
back:
	if( set_bits != 0 || useing != 0 ){
		session_triger_free( p_session, set_bits, useing );
	}
	session_ref_dec( p_session );
	return 0;
}

static int session_send( common_user_data p_user_data )
{
	session p_session;
	active_packet p_aop;
	int useing;
	int ret, need_send, set_bits, dec_packet;
	rs_block p_block;
	void * p_io, * p_point, * p_point_data;
	common_user_data p_user_data;
	io_pipe_interface p_io_i;
	pipe_interface p_pipe_i;

	useing = dec_packet = set_bits = 0;
	p_io = NULL;
	p_session = ( session )p_user_data->ptr;
	p_aop = &p_session->aop;
	pthread_mutex_lock( &p_session->op_mutex );
	if( !IS_SESSION_CLOSED( p_session ) ){
		p_io = p_session->p_io;
		if( p_io != NULL ){
			useing = SESSION_USEING_UNIT;
			p_session->status += useing;
			CALL_INTERFACE_FUNC( p_io, gc_interface_s, ref_inc );
		}
	}
	pthread_mutex_unlock( &p_session->op_mutex );
	if( __builtin_expect( p_io == NULL, 0 ) ){
		goto back;
	}
	p_pipe_i = FIND_INTERFACE( p_io, pipe_interface_s );
	ret = p_pipe_i->get_other_point_ref( p_io, &p_point, &p_point_data );
	if( __builtin_expect( ret < 0 , 0 ) ){
		goto back1;
	}
	p_io_i = FIND_INTERFACE( p_point, io_pipe_interface_s );
	while( !IS_SESSION_CLOSED( p_session ) ){
		need_send = p_aop->buf_len - p_aop->cur_buf_index;
		if( need_send > 0 ){
			ret = p_io_i->send( p_point, p_point_data
				, p_aop->buf + p_aop->cur_buf_index, need_send, 0 );
			if( ret > 0 ){
				p_aop->cur_buf_index += ret;
			}
			if( ret == SOCKET_NO_RES || ( ret > 0 && need_send > ret ) ){
				if( session_rs_continue( p_session
						, SESSION_SENDING, SESSION_CAN_SEND ) > 0 ){
					continue;
				}
				break;
			}else if( ret < 0 ){
				MOON_PRINT_MAN( ERROR, "recv error!" );
				set_bits = SESSION_GOAWAY_RECV;
				break;
			}
		}else if( p_aop->p_block != NULL ){
			p_block = p_aop->p_block;
			p_aop->p_block = p_block->next;
			p_block->next = NULL;
			if( p_aop->p_stream != NULL ){
				stream_rsblock_free( p_aop->p_stream, p_block );
			}else{
				session_rsblock_free( p_block );
			}
			if( p_aop->p_block != NULL ){
				p_aop->buf = ( char * )( p_aop->p_block + 1 );
				p_aop->cur_buf_index = 0;
				p_aop->buf_len = p_aop->p_block->len;
			}else{
				dec_packet = 1;
				end_process_send_packet( p_session, p_aop );
				reset_aiop( p_aop );
			}
		}else{
			p_user_data = NULL;
			pthread_mutex_lock( &p_session->op_mutex );
			p_session->packet_num -= dec_packet;
			if( !IS_SESSION_CLOSED( p_session )
				&& p_session->packet_num > 0 ){
				p_user_data = avl_leftest_node( p_session->p_ops_avl );
				avl_del2( &p_session->p_ops_avl, p_user_data );
				p_user_data = ( common_user_data )
					( ( char * )( p_user_data + 1 ) - SESSION_AVL_LEN );
				p_user_data->i32[ 1 ] = PIPE_STATUS_STOP;
				p_session->status &= ~STREAM_CAN_SEND;
			}else{
				p_session->status |= STREAM_CAN_SEND;
				p_session->status &= ~STREAM_SENDING;
			}
			pthread_mutex_unlock( &p_session->op_mutex );
			dec_packet = 1;
			if( p_user_data != NULL )
				if( pre_process_recv_packet( p_session, p_aop, p_user_data ) == 0 ){
					p_block = p_aop->p_block;
					p_aop->buf = ( char * )( p_block + 1 );
					p_aop->cur_buf_index = 0;
					p_aop->buf_len = p_block->len;
				}
				CALL_INTERFACE_FUNC( p_user_data, gc_interface_s, ref_dec );
			}else{
				break;
			}
		}
	}
	CALL_INTERFACE_FUNC( p_point, gc_interface_s, ref_dec );
	CALL_INTERFACE_FUNC( p_point_data, gc_interface_s, ref_dec );
back1:
	CALL_INTERFACE_FUNC( p_io, gc_interface_s, ref_dec );
back:
	if( set_bits != 0 || useing != 0 ){
		session_triger_free( p_session, set_bits, useing );
	}
	session_ref_dec( p_session );
	return 0;
}

static int session_recv_event( void * p_data, void * p_pipe )
{
	session p_session;
	int cant_read, error;

	p_session = p_data;
	cant_read = 0;
	pthread_mutex_lock( &p_session->op_mutex );
	if( !IS_SESSION_CLOSED( p_session ) ){
		cant_read = ( p_session->status & SESSION_RECVING ) / SESSION_RECVING;
		p_session->status |=  SESSION_RECVING | ( SESSION_CAN_RECV * cant_read  );
	}
	pthread_mutex_unlock( &p_session->op_mutex );
	if( cant_read == 0 ){
		if( start_rs_task_with_timer( p_session, SESSION_RECVING ) < 0 ){
			MOON_PRINT_MAN( ERROR, "session recv error!" );
			session_triger_free( p_session, SESSION_GOAWAY_RECV, 0 );
		}
	}
	return 0;
}

static int session_send_event( void * p_data, void * p_pipe )
{
	session p_session;
	int can_send, error;

	can_send = 0;
	p_session = p_data;
	pthread_mutex_lock( &p_session->op_mutex );
	if( !IS_SESSION_CLOSED( p_session ) ){
		p_session->status |= SESSION_CAN_SEND;
		can_send = try_start_send_task( p_session );
	}
	pthread_mutex_unlock( &p_session->op_mutex ):
	if( can_send ){
		if( start_rs_task_with_timer( p_session, SESSION_SENDING ) < 0 ){
			MOON_PRINT_MAN( ERROR, "session send error!" );
			session_triger_free( p_session, SESSION_GOAWAY_RECV, 0 );
		}
	}
	return 0;
}

//必需具备重复删除能力
static int session_pipe_close( void * p_data, void * p_pipe )
{
	session p_session;
	int set_bits, is_free;
	common_user_data p_category;
	void * p_tmp;
	avl_tree p_avl;

	set_bits = 0;
	p_session = p_data;
	p_category = p_pipe;
	pthread_mutex_lock( &p_session->op_mutex );
	if( !IS_SESSION_FREED( p_session ) 
		&& p_category->i32[ 1 ] == PIPE_STATUS_ACTIVE ){
		switch( p_category->i32[ 0 ] ){
		case SESSION_CONTROL:
			p_session->p_control = NULL;
			set_bits = SESSION_GOAWAY_RECV;
			break;
		case SESSION_RECV_TIMER:
		case SESSION_SEND_TIMER:
			//never happen
			p_session->p_timers[ p_category->i32[ 0 ] ] = NULL;
			set_bits = SESSION_GOAWAY_RECV;
			break;
		case SESSION_PING:
			p_session->p_ping = NULL;
			break;
		case SESSION_IO:
			set_bits = SESSION_GOAWAY_RECV;
			p_session->p_io = NULL;
			break;
		case SESSION_NEW_STREAM:
			p_tmp = ( list_to_data )( p_category + 1 );
			dlist_del( p_tmp );
			break;
		case SESSION_LINK_STREAM:
			p_avl = ( avl_tree )( p_category + 1 );
			avl_del2( &p_session->p_streams_avl, p_avl + 1 );
			break;
		case SESSION_OP_HUB:
			p_avl = ( avl_tree )( p_category + 1 );
			avl_del2( &p_session->p_ops_avl, p_avl + 1 );
			break;
		case SESSION_OP:
			set_bits = SESSION_GOAWAY_RECV;
			p_session->p_op = NULL;
			break;
		default:
			p_pipe = NULL;
			MOON_PRINT_MAN( ERROR
				, "illegal session pipe category:%d", p_category->i_num );
		}
		p_session->status |= set_bits;
		is_free = IS_SESSION_FREED( p_session );
	}else{
		p_pipe = NULL;
	}
	p_category->i32[ 1 ] = PIPE_STATUS_DEL;
	pthread_mutex_unlock( &p_session->op_mutex );
	if( p_pipe != NULL ){
		CALL_INTERFACE_FUNC( p_pipe, gc_interface_s, ref_dec );
	}
	if( is_free ){
		session_close_func( p_session );
	}
}

static void session_get_pipe_data_len( void * p_data, int * p_len )
{
	if( p_len != NULL ){
		*p_len = SESSION_PIPE_LEN;
	}
}

