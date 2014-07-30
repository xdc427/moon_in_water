#include"moon_debug.h"
#include"moon_common.h"
#include"moon_max_min_heap.h"
#include"common_socket.h"
#include"moon_pthread_pool.h"
#include"moon_packet2.h"
#include"moon_timer.h"
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<time.h>
#include<sys/types.h>
#include<pthread.h>

/*遵循下面三点，不会有错：
 *1.优先使用管道通讯。
 *2.先改变自身状态再向外发消息。
 *3.初始化时，你把一个东西置于可查寻区域，而自己还要使用时一定要有引用计数。
 */

/* 关于stream的初始化：
 * 对于正常的stream，加入session，然后session加入它，即可。
 * 对于pushed stream，先加入session，然后session加入它，然后自身加入main stream，接着main stream加入它。
 */

enum{
	FRAME_HEAD_LEN = 8,
	FRAME_SYN_REPLY_HEAD_LEN = 12,
	FRAME_SYN_HEAD_LEN = 20,
	FRAME_MAX_HEAD_LEN = FRAME_SYN_HEAD_LEN
};

enum{
	SYN_REPLY_ADDITION_LEN = FRAME_SYN_REPLY_HEAD_LEN - FRAME_HEAD_LEN,
	SYN_ADDITION_LEN = FRAME_SYN_HEAD_LEN - FRAME_HEAD_LEN,
	MAX_ADDITION_LEN = FRAME_MAX_HEAD_LEN - FRAME_HEAD_LEN
};

enum{
	FRAME_RST_LEN = 16,
	FRAME_PING_LEN = 12,
	FRAME_GOAWAY_LEN = 16,
	FRAME_WINDOW_UPDATE_LEN = 16
};

enum{
	FD_GOAWAY_RECV = 0x1,
	FD_GOAWAY_SEND = 0x2,
	FD_RECVING = 0x4,
	FD_SENDING = 0x8,
	FD_CAN_RECV = 0x10,
	FD_CAN_SEND = 0x20
};

typedef enum{
	STREAM_INITIAL,
	STREAM_SYN_RS,
	STREAM_SYNREPLY_RS,
	STREAM_CLOSING
} stream_status_e;

typedef enum{
	STREAM_USER_CLOSED = 0x1,
	STREAM_INTERNAL_CLOSED = 0x2,
	STREAM_CAN_RECV = 0x4,
	STREAM_RECVING = 0x8,
	STREAM_CAN_SEND = 0x10,
	STREAM_SENDING = 0x20,
	STREAM_SEND_TO_SESSION = 0x40
} stream_shut_flag;

typedef struct{
	stream p_stream;
	buf_head p_buf_head;
	int type;
	common_user_data_u user_data;
	int pri;
	unsigned long long seq;
	int split_index;
} output_packet_s;
typedef output_packet_s * output_packet;

typedef struct{
	int ( *data_arrive )( buf_head p_buf_head, common_user_data_u user_data );
	int ( *data_can_send )( common_user_data_u user_data );
	void ( *connect_close )( common_user_data_u user_data );
	void ( *user_data_ref_dec )( common_user_data_u user_data );
	void ( *user_data_ref_inc )( common_user_data_u user_data );
	common_user_data_u user_data;
} stream_callbacks_s;
typedef stream_callbacks_s * stream_callbacks;

//double_list_s + stream_pipe_s
//这是一个双体结构，两个stream各用一部分信息，但分配在同一块内存
typedef struct{
	int ref_num;
	stream p_main_stream;
	stream p_pushed_stream;
} stream_pipe_s;
typedef stream_pipe_s * stream_pipe;

#define IS_STREAM_CLOSED( p_stream ) ( p_stream->status == STREAM_CLOSING \
 && ( p_stream->shut_flags & ( STREAM_RECVING | STREAM_SENDING ) ) == 0 )

typedef struct stream_s{
	int ref_num;
	session p_session;
	int stream_id;
	int pri;
	unsigned status;
	unsigned shut_flags
	pthread_mutex_t deferred_mutex;
	max_min_heap p_deferred_op;
	double_list_s inpacket_head;//头的prev指向最后
	int window_size;
	int send_buf_limit;
	int send_buf_size;
	int recv_window_size;
	int is_pushed;//is a pushed stream
	int pushed_stream_limit;
	int pushed_stream_num;
	stream_pipe p_pipe;
	stream_callbacks_s callbacks;
	unsigned long long recv_timer;
	unsigned long long send_timer;
} stream_s;
typedef stream_s * stream;

typedef struct{
	unsigned char headbuf[ FRAME_HEAD_LEN ];
	int cur_head_in;
	unsigned char * buf;
	int cur_buf_in;
	int buf_len;
	int is_drop;//包过大则丢弃。
} active_input_packet_s;
typpedef active_input_packet_s * active_input_packet;

typedef struct{
	output_packet_s packet;
	unsigned char * buf;//指向packet里的buf
	int cur_buf_out;
	int buf_len;
} active_output_packet_s;
typedef active_output_packet_s * active_output_packet;

typedef struct{
	int ( *on_stream_first_data_recv )( stream p_stream );
} session_callbacks_s;

#define IS_SESSION_CLOSED( p_session ) ( ( p_session->status & FD_GOAWAY_RECV ) == FD_GOAWAY_RECV )
typedef struct{
	int ref_num;
	//streams map
	hash_table_s stream_table;
	//output packet
	max_min_heap op_heap;
	max_min_heap op_ss_heap;
	pthread_mutex_t op_mutex;
	//输入未满一帧时的临时数据
	active_input_packet_s aip;
	//输出未满一帧时的临时数据
	active_output_packet_s aop;
	unsigned long long next_seq;
	session_callbacks_s callbacks;
	int num_outgoing_streams;
	int num_incoming_streams;
	int next_stream_id;
	int last_recv_stream_id;
	int last_good_stream_id;
	unsigned next_ping_unique_id;
	unsigned last_ping_unique_id;
	int is_server;
	unsigned long long rs_timer[ 2 ];
	unsigned long long ping_timer;
	unsigned long long goaway_send_timer;
	unsigned status;
	int cur_rst_num;//限制对找不到stream发送rst包的个数
	socket_desc p_desc;
}session_s;
typedef session_s * session;

static int( *rs_func[ 2 ] )( common_user_data ) = { session_recv, session_send };
double_list_s session_list[ HASH_NUM ];
pthread_mutex_t session_mutex;

enum{
	INITIAL_WINDOW_SIZE = 2 * 1048 * 1024,
	INBOUND_BUFFER_LENGTH = INITIAL_WINDOW_SIZE >> 1 + MAX_ADDITION_LEN,
	MIN_PACKET_SPLIT_LEN = INITIAL_WINDOW_SIZE >> 1,
	MIN_SEND_WINDOW_SIZE = INITIAL_WINDOW_SIZE >> 2,
	FRAME_DROP_BUFFER_SIZE = 2 * 1024,
	SESSION_MAX_RST_MUN = 128
};

static int output_packet_pri_compare( void * p1, void * p1 )
{
	output_packet p_packet1, p_packet2;
	int cmp;

	p_packet1 = ( output_packet )p1;
	p_packet2 = ( output_packet )p2;
	if( ( cmp = p_packet1->pri - p_packet2->pri ) == 0 
		&& ( cmp = p_packet1->seq - p_packet2->seq ) == 0 ){
		cmp = p_packet1->split_index - p_packet2->split_index;
	}
	return cmp;
}

static int session_new( session p_session, const session_callbacks p_callbacks, socket_desc p_desc )
{
	memset( p_session, 0, sizeof( *p_session ) );
	p_session->op_heap = heap_init( 1, sizeof( output_packet_s ), 0, output_packet_pri_compare ); 
	if( p_session->op_heap ==  NULL ){
		MOON_PRINT_MAN( ERROR, "fail to create op heap!" );
		goto fail_op_heap;
	}
	p_session->op_ss_heap = heap_init( 1, sizeof( output_packet_s ), 0, output_packet_pri_compare ); 
	if( p_session->op_ss_heap == NULL ){
		MOON_PRINT_MAN( ERROR, "fail to create op ss heap!" );
		goto fail_op_ss_heap;
	}
	if( pthread_mutex_init( &p_session->op_mutex ) < 0 ){
		MOON_PRINT_MAN( ERROR, "init op mutex error!" );
		goto fail_op_mutex;
	}
	p_session->ref_num = 1;
	p_session->callbacks = *p_callbacks;
	p_session->p_desc = p_desc;
	
	return 0;
fail_op_mutex:
	heap_free( p_session->op_ss_heap );
fail_ob_ss_heap:
	heap_free( p_session->op_heap );
fail_ob_heap:
	return -1;
}

static void session_free( session p_session )
{
	output_packet_s packet;

	//op_heap
	while( heap_pop( p_session->op_heap, &packet ) >= 0 ){
		free_total_packet( packet.p_buf_head );
	}
	heap_free( p_session->op_heap );
	//op_ss_heap
	while( heap_pop( p_session->op_ss_heap, &packet ) >= 0 ){
		free_total_packet( packet.p_buf_head );
	}
	heap_free( p_session->op_ss_heap );
	//aop
	if( p_session->aop.buf != NULL ){
		free_total_packet( p_session->aop.packet.p_buf_head );
	}
	//aip
	IF_FREE( p_session->aip.buf );
	//op_mutex
	pthread_mutex_destroy( &p_session->op_mutex );
	//p_desc
	p_session->p_desc->close( p_session->p_desc );
	//验证
	if( p_session->ping_timer != 0 || p_session->goaway_send_timer != 0 
		p_session->rs_timer[ 0 ] != 0 || p_session->rs_timer[ 1 ] != 0 ){
		MOON_PRINT_MAN( ERROR, "timer error!" );
	}
	//free
	hash_free( ( socket_user_desc )p_session - 1 );
}

//服务端序列号为偶数，客户端为奇数
static int session_new_server( session p_session, session_callbacks p_callbacks, socket_desc p_desc )
{
	if( session_new( p_session, p_callbacks, p_desc ) < 0 ){
		MOON_PRINT_MAN( ERROR, "create session error!" );
		return -1;
	}
	p_session->is_server = 1;
	p_session->next_stream_id = 2;
	p_session->next_ping_unique_id = 2;
	return 0;
}

static int session_new_client( session p_session, session_callbacks p_callbacks, socket_desc p_desc )
{
	if( session_new( p_session, p_callbacks, p_desc ) < 0 ){
		MOON_PRINT_MAN( ERROR, "create session error!" );
		return -1;
	}
	p_session->next_stream_id = 1;
	p_session->next_ping_unique_id = 1;
	return 0;
}

static inline stream init_new_stream( int stream_id, int pri, int is_pushed, int send_buf_size )
{
	stream p_stream;

	p_stream = ( stream )hash_malloc( sizeof( *p_stream ) );
	if( p_stream == NULL ){
		goto malloc_error;
	}
	if( pthread_mutex_init( &p_stream->deferred_mutex ) != 0 ){
		goto mutex_error;
	}
	p_stream->p_deferred_op = heap_init( 1, sizeof( output_packet_s ), 0, output_packet_pri_compare );
	if( p_stream->p_deferred_op == NULL ){
		goto heap_error;
	}
	p_stream->ref_num = 1;
	p_stream->stream_id = stream_id;
	p_stream->pri = pri;
	p_stream->window_size = INITIAL_WINDOW_SIZE;
	p_stream->recv_window_size = INITIAL_WINDOW_SIZE;
	p_stream->is_pushed = is_pushed;
	p_stream->send_buf_size = send_buf_size;
	p_stream->send_buf_limit = send_buf_size;
	p_stream->inpacket_head.prev = &p_stream->inpacket_head;
	return p_stream;
heap_error:
	pthread_mutex_destroy( &p_stream->deferred_mutex );
mutex_error:
	hash_free( p_stream );
malloc_error:
	return NULL;
}

//下面这两个函数都是初始化时候用的
static int linked_stream_and_session( stream p_stream, session p_session )
{
	int ret;
	char id_buf[ 64 ];
	stream p_tmp_stream;

	ret = -1;
	snprintf( id_buf, sizeof( id_buf ), "%d", stream.stream_id );
	set_key_of_value( p_stream );
	
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( p_stream->status == STREAM_INITIAL ){
		session_ref_inc( p_session );
		p_stream->p_session = p_session;
		ret = 0;
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( ret < 0 ){
		MOON_PRINT_MAN( ERROR. "stream's status is not init!" );
		return ret;
	}
	ret = -1;
	pthread_mutex_lock( &p_session->op_mutex );
	if( !IS_SESSION_CLOSED( p_session ) ){
		if( stream.stream_id == 0 ){
			stream.stream_id = p_session->next_stream_id;
			p_session->next_stream_id += 2;
		}
		p_tmp_stream = ( stream )hash_table_search2( &p_session->stream_table, id_buf, p_stream );
		if( p_tmp_stream == p_stream ){
			ret = 0;
			stream_ref_inc( p_stream );
		}else{
			MOON_PRINT_MAN( ERROR, "stream add session error" );
		}
	}
	pthread_mutex_unlock( &p_session->op_mutex );
	return ret;
}

static void remove_pushed_stream( stream p_stream, stream_pipe p_pipe )
{
	stream_pipe p_del_pipe;

	p_del_pipe = NULL;
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( p_stream->status != STREAM_CLOSING ){
		if( p_stream->p_pipe == p_pipe ){
			p_stream->p_pipe = NULL;
			p_del_pipe = p_pipe;
			p_stream->pushed_stream_num--;
		}else if( is_really_del( p_pipe ) ){
			p_del_pipe = p_pipe;
			p_stream->pushed_stream_num--;
		}
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( p_del_pipe != NULL ){
		stream_ref_dec( p_del_pipe->p_pushed_stream );
		pipe_ref_dec( p_del_pipe );
	}
}

static void add_pushed_stream( stream p_stream, stream p_pushed_stream, stream_pipe p_pipe )
{
	int added;

	added = -1;
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( p_stream->status != STREAM_CLOSING 
		&& ( p_stream->shut_flags & ( STREAM_USER_CLOSED | STREAM_INTERNAL_CLOSED ) ) == 0 ){
		pipe_ref_inc( p_pipe );
		stream_ref_inc( p_pushed_stream );
		p_pipe->p_pushed_stream = p_pushed_stream;
		p_stream->p_pipe = dlist_insert( p_main_stream->p_pipe, p_pipe );
		p_stream->pushed_stream_num++;
		added = 0;
	}
	pthread_mutex_unlock( &p_main_stream->deferred_mutex );
	return added;
}

static int linked_pushed_and_main_stream( stream p_pushed_stream, stream p_main_stream )
{
	int ret, closed;
	stream_pipe p_pipe;

	ret = -1;
	p_pipe = dlist_malloc( sizeof( *p_pipe ) );
	if( p_pipe == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc pipe error!" );
		return ret;
	}
	p_pipe->ref_num = 1;
	pthread_mutex_lock( &p_pushed_stream->deferred_mutex );
	if( p_pushed_stream->status == STREAM_INITIAL ){
		pipe_ref_inc( p_pipe );
		stream_ref_inc( p_main_stream );
		p_pipe->p_main_stream = p_main_stream;
		p_pushed_stream->p_pipe = p_pipe;
		ret = 0;
	}
	pthread_mutex_unlock( &p_pushed_stream->deferred_mutex );
	if( ret < 0 ){
		MOON_PRINT_MAN( ERROR, "stream's status is not init!" );
		goto back;	
	}
	ret = add_pushed_stream( p_main_stream, p_pushed_stream, p_pipe );
	if( ret < 0 ){
		MOON_PRINT_MAN( ERROR, "add pushed stream to main stream error!" );
		goto back;
	}
	//有可能在中间时pushed_stream关闭了
	pthread_mutex_lock( &p_pushed_stream->deferred_mutex );
	closed = p_pushed_stream->status;
	pthread_mutex_unlock( &p_pushed_stream->deferred_mutex );
	if( closed ){
		MOON_PRINT_MAN( ERROR, "stream closed when linking!" ):
		remove_pushed_stream( p_main_stream, p_pipe );
	}
back:	
	pipe_ref_dec( p_pipe );
	return ret;
}

static int stream_recv_timer_func( common_user_data_u user_data, unsigned long long id )
{
	stream p_stream;
	int is_self;

	p_stream = user_data.ptr;
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( p_stream->recv_timer == id ){
		is_self = 1;
		p_stream->recv_timer = 0;
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( is_self ){
		MOON_PRINT_MAN( ERROR, "recv timeout!" );
		shut_stream( p_stream, INTERNAL_ERROR );
	}
	return is_self;
}

static int stream_send_timer_func( common_user_data_u user_data, unsigned long long id )
{
	stream p_stream;
	int is_self;

	p_stream = user_data.ptr;
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( p_stream->send_timer == id ){
		is_self = 1;
		p_stream->send_timer = 0;
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( is_self ){
		MOON_PRINT_MAN( ERROR, "send timeout!" );
		shut_stream( p_stream, INTERNAL_ERROR );
	}
	return is_self;
}

static int stream_rs_timer_free( common_user_data_u user_data, unsigned long long id )
{
	stream_ref_dec( ( stream )user_data.ptr );
}

static inline int stream_start_send_task( stream p_stream )
{
	process_task_s task;
	int ret;

	ret = -1;
	stream_ref_inc( p_stream );
	task.task_func = call_can_send_func;
	task.user_data[ 0 ].ptr = p_stream;
	if( put_task( &task ) < 0 ){
		MOON_PRINT_MAN( ERROR, "add recv packet task error!" );
		stream_ref_dec( p_stream );
		pthread_mutex_lock( &p_stream->deferred_mutex );
		p_stream->shut_flags &= ~STREAM_SENDING;
		pthread_mutex_unlock( &p_stream->deferred_mutex );
		shut_stream( p_stream, INTERNAL_ERROR );
	}else{
		ret = 0;
	}
	return ret;
}

#define STREAM_START_SENDTASK( p_stream ) ( p_stream->send_buf_size > 0\
 && ( p_stream->shut_flags & ( STREAM_CAN_SEND | STREAM_SENDING ) ) == STREAM_CAN_SEND )

static int user_set_can_send( stream p_stream, int timeout_ms )
{
	int can_send;
	unsigned long long del_timer, new_timer;
	timer_desc_s timer;

	can_recv = -1;
	del_timer = new_timer = 0;
	if( timeout_ms > 0 ){
		timer.time_ms = timeout_ms;
		timer.func = stream_rs_timer_func;
		timer.free = stream_rs_timer_free;
		stream_ref_inc( p_stream );
		timer.user_data.ptr = p_stream;
		if( timer_add( &timer, &new_timer, 0 ) < 0 ){
			MOON_PRINT_MAN( ERROR, "add to timer error!" );
			stream_ref_dec( p_stream );
			return -1;
		}
	}
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( p_stream->status != STREAM_CLOSING
		&& ( p_stream->shut_flags & ( STREAM_INTERNAL_CLOSED | STREAM_USER_CLOSED ) ) == 0 ){
		p_stream->shut_flags |= STREAM_CAN_SEND;
		del_timer = p_stream->send_timer;
		p_stream->send_timer = new_timer;
		if( ( can_send = STREAM_START_SENDTASK( p_stream ) ) != 0 ){
			p_stream->shut_flags |= STREAM_SENDING;
		}
	}else{
		del_timer = new_timer;
		new_timer = 0;
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( can_send > 0 ){
		can_send = stream_start_send_task( p_stream );
	}
	if( del_timer != 0 ){
		timer_del( del_timer );
	}
	if( new_timer != 0 ){
		timer_start( new_timer );
	}
	return can_send;
}

static int stream_send_packet( stream p_stream, buf_head p_buf_head, int flags )
{
	int added;
	output_packet_s packet;

	added = -1;
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( p_stream->status != STREAM_CLOSING 
		&& ( p_stream->shut_flags & ( STREAM_USER_CLOSED | STREAM_INTERNAL_CLOSED ) ) == 0 
		&& ( p_stream->is_pushed == 0 || ( p_stream->stream_id % 2 ) != p_stream->p_session->is_server ) ){
		if( ( flags == 0 && p_stream->send_buf_size > 0 )
			|| flags != 0 ){
			packet.p_buf_head = p_buf_head;
			packet.pri = p_stream->pri;
			packet.seq = __sync_fetch_and_add( &p_stream->p_session->next_seq, 1 );
			packet.split_index = 0;
			added = heap_push( p_stream->p_deferred_op, &packet );
			if( added >= 0 ){
				p_stream->send_buf_size -= get_packet_len( p_buf_head );
			}
		}
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( added >= 0 ){
		loop_send_packet( p_stream, 0 );
	}
	return added;
}

static inline int stream_start_recv_task( stream p_stream )
{
	process_task_s task;
	int ret;

	ret = -1;
	stream_ref_inc( p_stream );
	task.task_func = call_data_arrive_func;
	task.user_data[ 0 ].ptr = p_stream;
	if( put_task( &task ) < 0 ){
		MOON_PRINT_MAN( ERROR, "add recv packet task error!" );
		stream_ref_dec( p_stream );
		pthread_mutex_lock( &p_stream->deferred_mutex );
		p_stream->shut_flags &= ~STREAM_RECVING;
		pthread_mutex_unlock( &p_stream->deferred_mutex );
		shut_stream( p_stream, INTERNAL_ERROR );
	}else{
		ret = 0;
	}
	return ret;
}

#define STREAM_START_RECVTASK( p_stream ) ( p_stream->inpacket_head.next != NULL\
 && ( p_stream->shut_flags & ( STREAM_CAN_RECV | STREAM_RECVING ) ) == STREAM_CAN_RECV )

static int user_set_can_recv( stream p_stream, int timeout_ms )
{
	int can_recv;
	unsigned long long del_timer, new_timer;
	timer_desc_s timer;

	can_recv = -1;
	del_timer = new_timer = 0;
	if( timeout_ms > 0 ){
		timer.time_ms = timeout_ms;
		timer.func = stream_rs_timer_func;
		timer.free = stream_rs_timer_free;
		stream_ref_inc( p_stream );
		timer.user_data.ptr = p_stream;
		if( timer_add( &timer, &new_timer, 0 ) < 0 ){
			MOON_PRINT_MAN( ERROR, "add to timer error!" );
			stream_ref_dec( p_stream );
			return -1;
		}
	}
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( p_stream->status != STREAM_CLOSING 
		&& ( p_stream->shut_flags & STREAM_USER_CLOSED ) == 0 ){
		p_stream->shut_flags |= STREAM_CAN_RECV;
		del_timer = p_stream->recv_timer;
		p_stream->recv_timer = new_timer;
		if( ( can_recv = STREAM_START_RECVTASK( p_stream ) ) != 0 ){
			p_stream->shut_flags |= STREAM_RECVING;
		}
	}else{
		del_timer = new_timer;
		new_timer = 0;
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( can_recv > 0 ){
		can_recv = stream_start_recv_task( p_stream );
	}
	if( del_timer != 0 ){
		timer_del( del_timer );
	}
	if( new_timer != 0 ){
		timer_start( new_timer );
	}
	return can_recv;
}

//return: -1：error, 0 : only storage, 1 : can process
static inline int _inter_add_packet_to_stream( stream p_stream, buf_head * pp_head )
{
	int ret = -1;
	int len;

	len = get_packet_len( *pp_head );
	if( len > p_stream->recv_window_size ){
		MOON_PRINT_MAN( ERROR, "drop packet: more than recv window size!" );
		return ret;
	}
	dlist_append( list_to_data( p_stream->inpacket_head.prev ) );
	p_stream->inpacket_head.prev = data_to_list( pp_head );
	p_stream->recv_window_size -= len;
	ret = 0;
	if( ( ret = STREAM_START_RECVTASK( p_stream ) ) != 0 ){
		p_stream->shut_flags |= STREAM_RECVING;
	}
	return ret;
}

static int inter_add_syn_packet_to_stream( stream p_stream, buf_head p_buf_head )
{
	buf_head * pp_head = NULL;
	int error_code = 0;
	int start_recv = 0;

	pp_head = ( buf_head * )dlist_malloc( sizeof( *pp_head ) );
	if( pp_head == NULL ){
		error_code = INTERNAL_ERROR;
		goto back;
	}
	*pp_head = p_buf_head;
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( p_stream->status != STREAM_INITIAL 
		|| ( p_stream->shut_flags & ( STREAM_INTERNAL_CLOSED | STREAM_USER_CLOSED ) ) != 0 ){
		error_code = PROTOCOL_ERROR;
	}else if( ( start_recv = add_packet_to_stream( p_stream, pp_head ) ) < 0 ){
		error_code = FLOW_CONTROL_ERROR;
	}else{
		p_stream->status = STREAM_SYN_RS;
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( error_code != 0 ){
		dlist_free( pp_head );
	}else if( start_recv > 0 ){
		stream_start_recv_task( p_stream );
	}
back:
	return error_code;
}

static int inter_add_synreply_packet_to_stream( stream p_stream, buf_head p_buf_head )
{
	buf_head * pp_head = NULL;
	int error_code = 0;
	int start_recv = 0;

	pp_head = ( buf_head * )dlist_malloc( sizeof( *pp_head ) );
	if( pp_head != NULL ){
		error_code = INTERNAL_ERROR;
		goto back;
	}
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( ( stream_id % 2 ) == p_stream->p_session->is_server
		|| p_stream->status != STREAM_SYN_RS
		|| p_stream->is_pushed != 0
		|| ( p_stream->shut_flags & ( STREAM_INTERNAL_CLOSED | STREAM_USER_CLOSED ) ) != 0 ){
		error_code = PROTOCOL_ERROR;
	}else if( ( start_recv = add_packet_to_stream( p_stream, pp_head ) ) < 0 ){
		error_code = FLOW_CONTROL_ERROR;
	}else{
		p_stream->status = STREAM_SYNREPLY_RS;
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( error_code != 0 ){
		dlist_free( pp_head );
	}else if( start_recv > 0 ){
		stream_start_recv_task( p_stream );
	}
back:
	return error_code;
}

static int inter_add_data_packet_to_stream( stream p_stream, buf_head p_buf_head )
{
	buf_head * pp_head = NULL;
	int error_code = 0;
	int start_recv = 0;


	pp_head = ( buf_head * )dlist_malloc( sizeof( *pp_head ) );
	if( pp_head != NULL ){
		error_code = INTERNAL_ERROR;
		goto back;
	}
	*pp_head = p_buf_head;
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( ( ( p_stream->status != STREAM_SYN_RS || ( p_stream->stream_id % 2 ) != p_stream->is_server ) 
			&& p_stream->status != STREAM_SYNREPLY_RS )
		|| ( p_stream->shut_flags & ( STREAM_INTERNAL_CLOSED | STREAM_USER_CLOSED ) ) != 0 
		|| ( p_stream->is_pushed != 0 && ( p_stream->stream_id % 2 ) != p_stream->is_server ) ){
		error_code = PROTOCOL_ERROR;
	}else if( ( can_recv = add_packet_to_stream( p_stream, pp_head ) ) < 0 ){
		error_code = FLOW_CONTROL_ERROR;
	}	
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( error_code != 0 ){
		dlist_free( pp_head );
	}else if( start_recv > 0 ){
		stream_start_recv_task( p_stream );
	}
back:
	return error_code;
}

static void user_close_stream( stream p_stream, int is_shuted )
{
	int closed, send_rst;
	session p_session;

	closed = send_rst = 0;
	MOON_PRINT_MAN( ERROR, "stream inter error:%d", error_code );
	if( is_shuted != 0 ){
		return shut_stream( p_stream, 0 );
	}
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( p_stream->status != STREAM_CLOSING 
		&& ( p_stream->shut_flags & STREAM_USER_CLOSED ) == 0 ){
		p_stream->shut_flags |= STREAM_USERL_CLOSED;
		if( ( p_stream->shut_flags & STREAM_INTERNAL_CLOSED ) != 0 
			|| p_stream->send_buf_size = p_stream->send_buf_limit ){
			p_stream->status = STREAM_CLOSING;
			closed = IS_STREAM_CLOSED( p_stream );
			if( ( p_stream->shut_flags & STREAM_INTERNAL_CLOSED ) == 0 ){
				send_rst = 1;
				p_session = p_stream->p_session;
				p_session = session_ref_inc( p_session );
			}
		}
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( closed ){
		stream_close_func( p_stream );
	}
	if( send_rst ){
		add_rst_packet_to_session( p_session, p_stream->pri, INTERNAL_ERROR, p_stream->pri );
		session_ref_dec( p_session );
	}
}

static void shut_stream( stream p_stream, int error_code )
{
	int closed, send_rst;
	session p_session;

	close = send_rst = 0;
	MOON_PRINT_MAN( ERROR, "shut stream!" );
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( p_stream->status != STREAM_CLOSING ){
		p_stream->status = STREAM_CLOSING;
		closed = IS_STREAM_CLOSED( p_stream );
		if( ( p_stream->shut_flags & STREAM_INTERNAL_CLOSED ) == 0 ){
			p_session = p_stream->p_session;
			session_ref_inc( p_session );
			send_rst = 1;
		}
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( closed ){
		stream_close_func( p_stream );
	}
	if( send_rst ){
		add_rst_packet_to_session( p_session, p_stream->pri, error_code, p_stream->pri );
		session_ref_dec( p_session );
	}
}

static void inter_close_stream( stream p_stream, int error_code )
{
	int closed;

	closed = 0;
	MOON_PRINT_MAN( ERROR, "stream inter error:%d", error_code );
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( p_stream->status != STREAM_CLOSING 
		&& ( p_stream->shut_flags & STREAM_INTERNAL_CLOSED ) == 0 ){
		p_stream->shut_flags |= STREAM_INTERNAL_CLOSED;
		if( ( p_stream->shut_flags & STREAM_USER_CLOSED ) != 0 
			|| p_stream->inpacket_head.next == NULL ){
			p_stream->status = STREAM_CLOSING;
			closed = IS_STREAM_CLOSED( p_stream );
		}
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( closed ){
		stream_close_func( p_stream );
	}
}

static void inter_error_stream( stream p_stream, int error_code )
{
	int closed, send_rst;
	session p_session;

	closed = send_rst = 0;
	MOON_PRINT_MAN( ERROR, "stream inter error:%d", error_code );
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( p_stream->status != STREAM_CLOSING 
		&& ( p_stream->shut_flags & STREAM_INTERNAL_CLOSED ) == 0 ){
		p_stream->shut_flags |= STREAM_INTERNAL_CLOSED;
		if( ( p_stream->shut_flags & STREAM_USER_CLOSED ) != 0 
			|| p_stream->inpacket_head.next == NULL ){
			p_stream->status = STREAM_CLOSING;
			closed = IS_STREAM_CLOSED( p_stream );
		}
		p_session = p_stream->p_session;
		session_ref_inc( p_session );
		send_rst = 1;
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( closed ){
		stream_close_func( p_stream );
	}
	if( send_rst ){
		add_rst_packet_to_session( p_session, p_stream->pri, INTERNAL_ERROR, p_stream->pri );
		session_ref_dec( p_session );
	}
}

static void stream_free( stream p_stream )
{
	output_packet_s packet;
	buf_head p_buf_head, p_buf_tmp;

	//free deferred mutex
	pthread_mutex_destroy( &p_stream->deferred_mutex );
	//free deferred op
	while( heap_pop( p_stream->p_deferred_op, &packet ) >= 0 ){
		free_total_packet( packet.p_buf_head );
	}
	heap_free( p_stream->p_deferred_op );
	//free input packet
	p_buf_head = list_to_data( p_stream->inpacket_head.next );
	while( p_buf_head != NULL ){	
		p_buf_tmp = p_buf_head;
		p_buf_head = dlist_next( p_buf_head );
		dlist_free( p_buf_tmp );
	}
	//判断正确
	if( p_stream->recv_timer != 0 ){
		MOON_PRINT_MAN( ERROR, "stream free has error!" );
	}
	//free
	hash_free( p_stream );
}

static void stream_close_func( stream p_stream )
{
	stream_pipe p_pipe, p_tmp;

	notice_stream_closed( p_stream->p_session, p_stream );
	session_ref_dec( p_session );
	if( p_stream->is_pushed_stream ){
		if( p_stream->p_pipe != NULL ){
			remove_pushed_stream( p_stream->p_pipe->p_main_stream, p_stream->p_pipe );
			stream_ref_dec( p_stream->p_pipe->p_main_stream );
			pipe_ref_dec( p_stream->p_pipe );
		}
	}else{
		for( p_pipe = p_stream->p_pipe; p_pipe != NULL; ){
			p_tmp = p_pipe;
			p_pipe = dlist_next( p_pipe );
			shut_stream( p_tmp->p_pushed_stream, 0 );
			stream_ref_dec( p_tmp->p_pushed_stream );
			pipe_ref_dec( p_tmp );
		}
	}
	if( ( p_stream->shut_flags & STREAM_USER_CLOSED ) == 0 ){
		CALL_FUNC( p_stream->callbacks.connect_close, p_stream->callbacks.user_data );
	}
	CALL_FUNC( p_stream->callbacks.user_data_ref_dec, p_stream->callbacks.user_data );
	if( p_stream->recv_timer  != 0 ){
		timer_del( p_stream->recv_timer );
		p_stream->recv_timer = 0;
	}
	if( p_stream->send_timer != 0 ){
		timer_del( p_stream->send_timer );
		p_stream->send_timer = 0;
	}
}

static inline void session_ref_inc( session p_session )
{
	__sync_fetch_and_add( &p_session->ref_num, 1 );
}

static inline void session_ref_dec( session p_session )
{
	int tmp;

	tmp = __sync_sub_and_fetch( &p_session->ref_num, 1 );
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

static inline void notice_stream_closed( session p_session, stream p_stream )
{
	int is_close = 0;

	pthread_mutex_lock( &p_session->op_mutex );
	if( !IS_SESSION_CLOSED( p_session ) ){
		is_close = is_hash_table_really_del( &p_session->stream_table, p_stream );
	}
	pthread_mutex_unlock( &p_session->op_mutex );
	if( is_close ){
		stream_ref_dec( p_stream );
	}
}

static void stream_ref_dec( stream p_stream )
{
	int tmp;

	tmp = __sync_sub_and_fetch( &p_stream->ref_num, 1 );
	if( tmp == 0 ){
		if( !IS_STREAM_CLOSED( p_stream ) ){
			MOON_PRINT_MAN( ERROR, "stream not closed when free!" );
		}
		stream_free( p_stream );
	}else if( tmp < 0 ){
		MOON_PRINT_MAN( ERROR, "stream ref num under overflow!" );
	}
}

static inline void stream_ref_inc( stream p_stream )
{
	__sync_fetch_and_add( &p_stream->ref_num, 1 );
}

static inline void pipe_ref_inc( stream_pipe p_pipe )
{
	__sync_fetch_and_add( &p_pipe->ref_num, 1 );
}

static inline void pipe_ref_dec( stream_pipe p_pipe )
{
	int tmp;

	tmp = __sync_sub_and_fetch( &p_pipe->ref_num, 1 );
	if( tmp == 0 ){
		dlist_free( p_pipe );
	}else if( tmp < 0 ){
		MOON_PRINT_MAN( ERROR, "stream pipe ref num under overflow!" );
	}
}

int send_packet( common_user_data_u user_data, buf_head p_buf_head, int send_anyway )
{

}

int recv_next_packet( common_user_data_u user_data, int timeout_ms )
{

}

int send_next_packet( common_user_data_u user_data, int timeout_ms )
{

}

int stream_close( common_user_data_u user_data, int is_shuted )
{

}

int is_pushed_stream( common_user_data_u user_data )
{

}

int get_main_stream( common_user_data_u user_data )
{

}

int get_pushed_stream( common_user_data_u user_data, int is_new )
{

}

#define SESSION_VERSION 1
enum{
	TYPE_SYN = 0x1,
	TYPE_SYN_REPLY = 0x2,
	TYPE_RST = 0x3,
	TYPE_PING = 0x4,
	TYPE_GOAWAY = 0x5,
	TYPE_WINDOW_UPDATE = 0x6
};

enum{
	FLAG_FIN = 0x01,
	FLAG_UNDIRECTIONAL = 0x02
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
	STREAM_ALREADY_CLOSED,//端点半关闭时，收到数据帧
	FRAME_TOO_LARGE
};

enum{
	HIGHEST_PRI,
	USER_PRI1,
	USER_PRI2,
	USER_PRI3,
	USER_PRI4,
	USER_PRI5,
	USER_PRI6,
	LOWEST_PRI
};

#define IS_CONTROL( head ) ( *( uint32_t * )( head ) & 0x80000000 )
#define SET_CONTROL( num ) ( ( uint32_t )( num ) | 0x80000000 )
#define CLEAR_CONTROL( num ) ( ( uint32_t )( num ) & ( ~0x80000000 ) )
//control frame
#define GET_VERSION( head ) ( ( *( uint32_t * )( head ) & 0x7fff0000 ) >> 16 )
#define SET_VERSION( num, ver ) ((  ( uint32_t )( num ) & ( ~0x7fff0000 ) )\
 | ( ( ( uint32_t )( ver ) << 16 ) & 0x7fff0000 ) )
#define GET_TYPE( head ) ( *( uint32_t * )( head ) & 0xffff )
#define SET_TYPPE( num, type ) ( ( ( uint32_t )( num ) & ( ~0xffff ) )\
 | ( ( uint32_t )( ver ) & 0xffff ) )
//
//data frame
#define GET_STREAM_ID( head ) ( *( uint32_t * )( head ) & 0x7fffffff )
//
//syn frame
#define GET_STREAM_PRI( head ) ( ( *( uint32_t * )( head ) & 0xe0000000 ) >> 29 )
#define SET_STREAM_PRI( num, pri ) ( ( ( uint32_t )num & ( ~0xe0000000 ) )\
 | ( ( uint32_t )( pri ) << 29 ) )
//
#define GET_FLAGS( head ) ( ( ( ( uint32_t * )( head ) )[ 1 ] & 0xff000000 ) >> 24 )
#define SET_FLAGS( num, flags ) ( ( ( uint32_t )( num ) & ( ~0xff000000 ) )\
 | ( ( uint32_t )( flags ) << 24 ) )
#define GET_LENGTH( head ) ( ( ( uint32_t * )( head ) )[ 1 ] & 0xffffff )
#define SET_LENGTH( num, len ) ( ( ( uint32_t )( num ) & ( ~0xffffff ) )\
 | ( ( uint32_t )( len ) & ( 0xffffff ) ) )

static inline char * malloc_data_frame_head( int stream_id, int data_len, int flags )
{
	char * buf;
	uint32_t * p_u32;

	buf = ( char * )packet_buf_malloc( FRAME_HEAD_LEN );
	if( buf == NULL ){
		return buf;
	}
	memset( buf, 0 , FRAME_HEAD_LEN );
	p_u32 = ( uint32_t * )buf;
	*p_u32 = CLEAR_CONTROL( stream_id );
	convert_uint32_t( p_u32 );
	p_u32++;
	*p_u32 =  SET_LENGTH( SET_FLAGS( 0, flags ), data_len );
	convert_uint32_t( p_u32 );
	return buf;

}

static inline void set_frame_control_head( uint32_t ** pp_u32, int type, int flags, int len )
{
	**pp_u32 = SET_TYPPE( SET_VERSION( SET_CONTROL( 0 ), SESSION_VERSION ), type );
	convert_uint32_t( *pp_u32 );
	( *pp_u32 )++;
	**pp_u32 = SET_LENGTH( SET_FLAGS( 0, flags ), len );
	convert_uint32_t( *pp_u32 );
	( *pp_u32 )++;
}

static inline void set_uint32( uint32_t ** pp_u32, int num )
{
	**pp_u32 = num;
	convert_uint32_t( *pp_u32 );
	( *pp_u32 )++;
}

static inline int malloc_control_frame( buf_head * pp_head, uint32_t ** pp_u32, int len )
{
	buf_head p_head;
	uint32_t * p_u32;
	buf_desc_s buf_desc;

	if( ( p_u32 = ( uint32_t * )packet_buf_malloc( len ) ) == NULL ){
		goto malloc_error;
	}
	p_head = NULL;
	buf_desc.offset = 0;
	buf_desc.len = len;
	buf_desc.buf = ( unsigned char * )p_u32;
	if( add_buf_to_packet( &p_head, &buf_desc ) < 0 ){
		goto add_error;
	}
	*pp_head = p_head;
	*pp_u32 = p_u32;
	return 0;
add_error:
	packet_buf_free( p_u32 );
malloc_error:
	return -1;
}

static inline buf_head create_rst_frame( int stream_id, int status_code )
{
	uint32_t * p_u32;
	buf_head p_buf_head = NULL;

	if( malloc_control_frame( &p_buf_head, &p_u32, FRAME_RST_LEN ) >= 0 ){
		set_frame_control_head( &p_u32, TYPE_RST, 0, FRAME_RST_LEN - FRAME_HEAD_LEN );
		set_uint32( &p_u32, stream_id );
		set_uint32( &p_u32, status_code );
	}
	return p_buf_head;
}

static inline buf_head create_ping_frame( unsigned ping_id )
{
	buf_head p_buf_head = NULL;
	uint32_t * p_u32;

	if( malloc_control_frame( &p_buf_head, &p_u32, FRAME_PING_LEN ) >= 0 ){
		set_frame_control_head( &p_u32, TYPE_PING, 0, FRAME_PING_LEN - FRAME_HEAD_LEN );
		set_uint32( &p_u32, ping_id );
	}
	return p_buf_head;
}

static inline buf_head create_goaway_frame( int last_good_stream_id, int status_code )
{
	buf_head p_buf_head = NULL;
	uint32_t * p_u32;
	
	if( malloc_control_frame( &p_buf_head, &p_u32, FRAME_GOAWAY_LEN ) >= 0 ){
		set_frame_control_head( &p_u32, TYPE_GOAWAY, 0, FRAME_GOAWAY_LEN - FRAME_HEAD_LEN );
		set_uint32( &p_u32, last_good_stream_id );
		set_uint32( &p_u32, status_code );
	}
	return p_buf_head;
}

static inline buf_head create_window_update_frame( int stream_id, unsigned delta_window_size )
{
	buf_head p_buf_head = NULL;
	uint32_t * p_u32;
	
	if( malloc_control_frame( &p_buf_head, &p_u32, FRAME_WINDOW_UPDATE_LEN ) >= 0 ){
		set_frame_control_head( &p_u32, TYPE_WINDOW_UPDATE, 0, FRAME_WINDOW_UPDATE_LEN - FRAME_HEAD_LEN );
		set_uint32( &p_u32, stream_id );
		set_uint32( &p_u32, delta_window_size );
	}
	return p_buf_head;
}

static inline char * malloc_syn_head_freame( int stream_id, int flags, int data_len, int assoc_id, int pri )
{
	char * buf;
	uint32_t * p_u32;
	int pri_num = 0;

	buf = ( char * )packet_buf_malloc( FRAME_SYN_HEAD_LEN );
	if( buf == NULL ){
		return buf;
	}
	p_u32 = ( uint32_t * )buf;
	set_frame_control_head( &p_u32, TYPE_SYN, flags, FRAME_SYN_HEAD_LEN - FRAME_HEAD_LEN + data_len );
	set_uint32( &p_u32, stream_id );
	set_uint32( &p_u32, assoc_id );
	pri_num = SET_STREAM_PRI( pri_num, pri );
	set_uint32( &pp_u32, pri_num );
	return buf;
}

static inline char * malloc_synreply_head_freame( int stream_id, int flags, int data_len )
{
	char * buf;
	uint32_t * p_u32;

	buf = ( char * )packet_buf_malloc( FRAME_SYN_REPLY_HEAD_LEN );
	if( buf == NULL ){
		return buf;
	}
	p_u32 = ( uint32_t * )buf;
	set_frame_control_head( &p_u32, TYPE_SYN_REPLY, flags, FRAME_SYN_REPLY_HEAD_LEN - FRAME_HEAD_LEN + data_len );
	set_uint32( &p_u32, stream_id );
	return buf;
}

static int call_data_arrive_func( common_user_data p_user_data )
{
	stream p_stream;
	stream_callbacks_s * p_callbacks;
	session p_session;
	unsigned long long timer_id;
	buf_head * pp_head;
	buf_head p_buf_head;
	int is_ok, is_del;

repeat:
	is_ok = is_del = 0;
	timer_id = 0;
	pp_head = NULL;
	p_stream = ( stream )p_user_data->ptr;
	p_callbacks = &p_stream->callbacks;
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( p_stream->status != STREAM_CLOSING 
		&& ( p_stream->shut_flags & STREAM_USER_CLOSED ) == 0 
		&& ( p_stream->shut_flags & STREAM_CAN_RECV ) == STREAM_CAN_RECV
		&& p_stream->inpacket_head.next != NULL ){
		is_ok++;
		//清除stream recv timer
		timer_id = p_stream->recv_timer;
		p_stream->recv_timer = 0;
		if( p_stream->inpacket_head.prev == p_stream->inpacket_head.next ){
			p_stream->inpacket_head.prev = &p_stream->inpacket_head;
		}
		pp_head = list_to_data( p_stream->inpacket_head.next );
		dlist_del( pp_head );
		CALL_FUNC( p_callbacks->user_data_ref_inc, p_callbacks->user_data );
		p_session = p_stream->p_session;
		session_ref_inc( p_session );
		p_stream->shut_flags &= ~STREAM_CAN_RECV;
	}else{
		p_stream->shut_flags &= ~STREAM_RECVING;
		is_del = IS_STREAM_CLOSED( p_stream );
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( timer_id != 0 ){
		timer_del( timer_id );
	}
	if( is_ok ){
		p_callbacks->data_arrive( *pp_head, p_callbacks->user_data );
		//send window_update
		if( add_winupdate_packet_to_session( p_session, p_stream, sizeof( *pp_head ) ) < 0 ){
			MOON_PRINT_MAN( ERROR, "create window update packet error!" );
			inter_error_stream( p_stream, INTERNAL_ERROR );
		}
		dlist_free( pp_head );
		CALL_FUNC( p_callbacks->user_data_ref_dec, p_callbacks->user_data );
		session_ref_dec( p_session );
		goto repeat;
	}
	if( is_del ){
		stream_close_func( p_stream );
	}
	stream_ref_dec( p_stream );
	return 0;
}

static int call_can_send_func( common_user_data p_user_data )
{
	stream p_stream;
	stream_callbacks_s * p_callbacks;
	session p_session;
	unsigned long long timer_id;
	int is_ok, is_del;

repeat:
	is_ok = is_del = 0;
	timer_id = 0;
	p_stream = ( stream )p_user_data->ptr;
	p_callbacks = &p_stream->callbacks;
	pthread_mutex_lock( &p_stream->deferred_mutex );
	if( p_stream->status != STREAM_CLOSING 
		&& ( p_stream->shut_flags & ( STREAM_INTERNAL_CLOSED | STREAM_USER_CLOSED ) ) == 0 
		&& ( p_stream->shut_flags & STREAM_CAN_SEND ) == STREAM_CAN_SEND
		&& p_stream->send_buf_size > 0 ){
		is_ok++;
		//清除stream recv timer
		timer_id = p_stream->send_timer;
		p_stream->send_timer = 0;
		CALL_FUNC( p_callbacks->user_data_ref_inc, p_callbacks->user_data );
		p_session = p_stream->p_session;
		session_ref_inc( p_session );
		p_stream->shut_flags &= ~STREAM_CAN_SEND;
	}else{
		p_stream->shut_flags &= ~STREAM_SENDING;
		is_del = IS_STREAM_CLOSED( p_stream );
	}
	pthread_mutex_unlock( &p_stream->deferred_mutex );
	if( timer_id != 0 ){
		timer_del( timer_id );
	}
	if( is_ok ){
		p_callbacks->data_can_send( p_callbacks->user_data );
		CALL_FUNC( p_callbacks->user_data_ref_dec, p_callbacks->user_data );
		session_ref_dec( p_session );
		goto repeat;
	}
	if( is_del ){
		stream_close_func( p_stream );
	}
	stream_ref_dec( p_stream );
	return 0;
}

static inline buf_head packet_recv_packet( char * buf, int len, int cur_elem_len )
{
	buf_desc_s buf_desc;
	buf_head p_buf_head;

	buf_desc.buf = buf;
	buf_desc.len = len - cur_elem_len;
	buf_desc.offset = cur_elem_len;
	p_buf_head = NULL;
	add_buf_to_packet( &p_buf_head, &buf_desc );
	return p_buf_head;
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
		p_stream->recv_window_size += p_packet->>user_data.i_num;
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

static inline int get_top_output_packet( session p_session )
{
	output_packet_s packet1, packet2;
	active_output_packet p_active_out;

	p_active_out = &p_session->aop;
	packet1.pri = LOWEST_PRI + 1;
	packet2.pri = LOWEST_PRI + 1;
	heap_top( p_session->op_ss_heap, &packet1 );
	heap_top( p_session->op_heap, &packet2 );
	if( packet1.pri <= packet2.pri ){
		if( packet1.pri == LOWEST_PRI + 1 ){
			return 0;
		}
		heap_pop( p_session->op_ss_heap, &packet1 );
	}else{
		heap_pop( p_session->op_heap, &packet1 );
	}
	p_active_out->packet = packet1;
	return 1;
}

static int session_send( common_user_data p_user_data )
{
	session p_session;
	int is_have, send_again;
	active_output_packet p_aop;
	socket_desc p_desc;

	p_session = ( session )p_user_data->ptr;
	p_aop = &p_session->aop;
	p_desc = p_session->p_desc;
	while( ( p_session->status & FD_GOAWAY_RECV ) == 0 ){
		if( p_session->aop.buf == NULL ){
			pthread_mutex_lock( &p_session->op_mutex );
			is_have = get_top_output_packet( p_session );
			p_session->status |= FD_CAN_SEND;
			p_session->status &= ~FD_SENDING;
			p_session->status |= FD_SENDING * is_have;
			p_session->status &= ~( FD_CAN_SEND * is_have );
			pthread_mutex_unlock( &p_session->op_mutex );
			if( is_have == 0 ){
				break;
			}
			if( pre_process_packet( &p_aop->packet ) < 0 ){
				MOON_PRINT_MAN( WARNNING, "drop a packet!" );
				free_total_packet( p_aop->packet.p_buf_head );
				continue;
			}
			begin_travel_packet( p_aop->packet.p_buf_head );
			if( get_next_buf( p_aop->packet.p_buf_head, &p_aop->buf, &p_aop->buf_len ) <= 0 ){
				MOON_PRINT_MAN( ERROR, "a invalid packet!" );
				free_total_packet( p_aop->packet.p_buf_head );
				continue;
			}
		}else{
			ret = p_desc->send( p_desc, p_aop->buf + p_aop->cur_buf_out
				, p_aop->buf_len - p_aop->cur_buf_out );
			if( ret > 0 ){
				p_aop->cur_buf_out += ret;
				if( p_aop->cur_buf_out < p_aop->buf_len ){
					ret = SOCKET_NO_RES;
				}
			}
			if( ret < 0 ){
				if( ret != SOCKET_NO_RES ){
					MOON_PRINT_MAN( ERROR, "send error %d!", ret );
				}
				pthread_mutex_lock( &p_session->op_mutex );
				send_again = p_session->status & FD_CAN_SEND;
				p_session->status &= ~( FD_CAN_SEND | FD_SENDING );
				p_session->status |= FD_SENDING * ( send_again / FD_CAN_SEND );
				pthread_mutex_unlock( &p_session->op_mutex );
				if( !send_again ){
					break;
				}
			}else{
				p_aop->buf = NULL;
				p_aop->buf_len = p_aop->cur_buf_out = 0;
				if( get_next_buf( p_aop->packet.p_buf_head, &p_aop->buf, &p_aop->buf_len ) <= 0 ){
					free_total_packet( p_aop->packet.p_buf_head );
				}
			}	
		}
	}
	session_ref_dec( p_session );
	return 0;
}

static void session_rs_timer_func( common_user_data_u user_data, unsigned long long id )
{
	session p_session;
	process_task p_task;
	int is_self;
	unsigned type;

	p_task = ( process_task )user_data.ptr;
	p_session = ( session )p_task->user_data[ 0 ].ptr;
	type = p_task->user_data[ 1 ].i_num;
	is_self = 0;
	pthread_mutex_lock( &p_session->op_mutex );
	if( p_session->rs_timer[ type >> 3 ] == id ){
		p_session->rs_timer[ type >> 3 ] = 0;
		is_self = 1;
	}else if( p_session->rs_timer[ type >> 3 ] != 0 ){
		MOON_PRINT_MAN( ERROR, "recv timer error!" );
	}
	pthread_mutex_unlock( &pp_session->op_mutex );
	if( ( p_session->status & FD_GOAWAY_RECV ) != 0 
		|| is_self == 0 ){
		goto back;
	}
	if( put_task( p_task ) < 0 ){
		MOON_PRINT_MAN( ERROR, "put recv task error again!" );
		if( add_session_rs_timer( p_session, p_task ) < 0 ){
			pthread_mutex_lock( &p_session->op_mutex );
			p_session->status &= ~type;
			pthread_mutex_unlock( &p_session->op_mutex );
			MOON_PRINT_MAN( ERROR, "put recv timer error!" );
			goto back;
		}
	}
	goto free;
back:
	session_ref_dec( p_session );
free:
	free( p_task );
}

static void session_rs_timer_free( common_user_data_u user_data )
{
	process_task p_task;

	p_task = ( process_task )user_data.ptr;
	session_ref_dec( ( session )p_task->user_data[ 0 ].ptr );
	free( p_task );
}

static int add_session_rs_timer( session p_session, process_task p_task )
{
	process_task p_malloc_task;
	timer_desc_s timer;
	unsigned long long id;
	unsigned type;

	p_malloc_task = malloc( sizeof( *p_malloc_task ) );
	if( p_malloc_task == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		goto malloc_error;
	}
	type = p_task->user_data[ 1 ].i_num;
	*p_malloc_task = *p_task; 
	timer.time_ms = 1000 * 2;//try after 2s
	timer.func = session_rs_timer_func;
	timer.free = session_rs_timer_free;
	timer.user_data.ptr = p_malloc_task;
	if( timer_add( &timer, &id, 0 ) < 0 ){
		MOON_PRINT_MAN( ERROR, "add to timer error!" );
		goto add_timer_error;
	}
	pthread_mutex_lock( &p_session->op_mutex );
	p_session->rs_timer[ type >> 3 ] = id;
	pthread_mutex_unlock( &p_session->op_mutex );
	timer_start( id );
	return 0;
add_timer_error:
	free( p_malloc_task );
malloc_error:
	return -1; 
}

static inline int try_start_send_task( session p_session )
{
	if( ( p_session->status & ( FD_SENDING | FD_CAN_SEND ) ) == FD_CAN_SEND
		&& ( p_session->aop.buf != NULL 
			|| heap_length( p_session->op_heap ) > 0 
			|| heap_length( p_session->op_ss_heap ) > 0 ) ){
		p_session->status @= ~FD_CAN_SEND;
		p_session->status |= FD_SENDING;
		return 0;
	}
	return -1;
}

static inline void start_rs_task( session p_session, unsigned type )
{
	process_task_s task;

	session_ref_inc( p_session );
	task.task_func = rs_func[ type >> 3 ];
	task.user_data[ 0 ].ptr = p_session;
	task.user_data[ 1 ].i_num = type;
	if( put_task( &task ) < 0 ){
		MOON_PRINT_MAN( WARNNING, "put rs task error!" );
		//加一个定时器任务，隔一段时间后再试
		if( add_session_rs_timer( p_session, &task ) < 0 ){
			MOON_PRINT_MAN( ERROR, "add rs timer error!" );
			session_ref_dec( p_session );
			pthread_mutex_lock( &p_session->op_mutex );
			p_session->status &= ~type;
			pthread_mutex_unlock( &p_session->op_mutex );
		}
	}
}

static int add_packet_to_session( session p_session, output_packet p_out_packet, int is_syn )
{
	int ret, can_send;
	
	if( p_session->status & FD_GOAWAY_RECV ){
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
	if( can_send >= 0 ){
		start_rs_task( p_session, FD_SENDING );
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

static inline int add_rst_packet_to_session( session p_session, int stream_id, int error_code, int pri )
{
	output_packet_s packet;
	int ret;

	ret = -1;
	if( ( packet.p_buf_head = create_rst_frame( stream_id, error_code ) ) != NULL ){
		packet.pri = pri;
		packet.type = TYPE_RST;
		if( add_packet_to_session( p_session, &packet, 0 ) < 0 ){
			free_total_packet( packet.p_buf_head );
		}else{
			ret = 0;
		}
	}
	return ret;
}

static inline int add_ping_packet_to_session( session p_session, unsigned ping_id )
{
	output_packet_s packet;
	int ret;

	ret = -1;
	if( ( packet.p_buf_head = create_ping_frame( ping_id ) ) != NULL ){
		packet.pri = HIGHEST_PRI;
		packet.type = TYPE_PING;
		if( add_packet_to_session( p_session, &packet, 0 ) < 0 ){
			free_total_packet( packet.p_buf_head );
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
				buf_desc.buf = malloc_syn_head_freame( p_stream->stream_id, 0, len, assoc_id, p_stream->pri )
			}else if( p_stream->status == STREAM_SYN_RS 
			&& ( p_stream->stream_id % 2 ) == p_stream->p_session->is_server ){
				p_stream->status = STREAM_SYNREPLY_RS;
				packet.type = TYPE_SYN_REPLY;
				buf_desc.offset = 0;
				buf_desc.len = FRAME_SYN_REPLY_HEAD_LEN;
				buf_desc.buf = malloc_synreply_head_freame( p_stream->stream_id, 0, len );
			}else{
				packet.type = 0;
				buf_desc.offset = 0;
				buf_desc.len = FRAME_HEAD_LEN;
				buf_desc.offset = 0;
				buf_desc.len = FRAME_HEAD_LEN;
				buf_desc.buf = malloc_data_frame_head( p_stream->stream_id, 0, len );
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

static int _close_streams( void * ptr, common_user_data_u user_data )
{
	stream p_stream;

	p_stream = ( stream )ptr;
	inter_close_stream( p_stream, 0 );
	stream_ref_dec( p_stream );
	return 0;
}

static void session_close_func( session p_session )
{
	common_user_data_u user_data;

	hash_table_traver( &p_session->stream_table, _close_streams, user_data );
	session_leave_hash_table( p_session );
	session_leave_listen( p_session );
	if( p_session->rs_timer[ 0 ] != 0 ){
		timer_del( p_session->rs_timer[ 0 ] );
		p_session->rs_timer[ 0 ] = 0;
	}
	if( p_session->rs_timer[ 1 ] != 0 ){
		timer_del( p_session->rs_timer[ 1 ] );
		p_session->rs_timer[ 1 ] = 0;
	}
	if( p_session->ping_timer != 0 ){
		timer_del( p_session->ping_timer );
		p_session->ping_timer = 0;
	}
}

static void close_session( session p_session )
{
	int is_close = 0;

	pthread_mutex_lock( &p_session->op_mutex );
	if( !IS_SESSION_CLOSED( p_session ) ){
		p_session->status |= FD_GOAWAY_RECV;
		is_close = 1;
	}
	pthread_mutex_unlock( &p_session->op_mutex );
	if( is_close ){
		session_close_func( p_session );
	}
}

static void ping_send_timer_func( common_user_data user_data, unsigned long long id )
{
	int is_ok;
	session p_session;
	timer_desc_s timer;
	unsigned long long timer_id = 0;

	p_session = ( session )user_data.ptr;
	timer.func = ping_wait_timer_func;
	timer.free = ping_timer_free;
	timer.user_data.ptr = p_session;
	if( ( is_ok = timer_add( &timer, &timer_id, 0 ) ) < 0 ){
		MOON_PRINT_MAN( ERROR, "fatal error: add ping timer error!" );
	}
	pthread_mutex_lock( &p_session->op_mutex );
	if( p_session->ping_timer == id ){
		if( is_ok < 0 ){
			p_session->ping_timer = 0;
		}else{
			p_session->ping_timer = timer_id;
			p_session->last_ping_unique_id = p_session->next_ping_unique_id;
			p_session->next_ping_unique_id += 2;
		}
	}else{
		is_ok = -1;
		MOON_PRINT_MAN( ERROR, "fatal error: ping send timer error!" );
	}
	pthread_mutex_unlock( &p_session->op_mutex ); 
	if( is_ok ){
		timer_start( timer_id );
	}else{
		if( timer_id != 0 ){
			timer_del( timer_id);
		}
		session_ref_dec( p_session );
	}
}

static void ping_wait_timer_func( common_user_data user_data, unsigned long long id )
{
	session p_session;
	int is_timeout;

	is_timeout = 0;
	p_session = ( session )user_data.ptr;
	pthread_mutex_lock( &p_session->op_mutex );
	if( p_session->ping_timer == id ){
		p_session->ping_timer = 0;
		p_session->last_ping_unique_id = 0;
		is_timeout = 1;
	}
	pthread_mutex_unlock( &p_session->op_mutex );
	if( is_timeout ){
		MOON_PRINT_MAN( ERROR, "ping wait timeout!" );
		close_session( p_session );
	}
	session_ref_dec( p_session );
}

static void ping_timer_free( common_user_data user_data )
{
	session_ref_dec( ( session )user_data.ptr );
}

static int process_recv_packet( session p_session, char * buf, int len )
{
	stream p_stream, p_assoc_stream;
	int stream_id, assoc_id, last_good_stream_id;
	unsigned status_code, delta_window_size, ping_id;
	int pri, flags, ret, error_code;
	buf_head p_buf_head;
	process_task_s task;
	uint32_t * p_u32;

	ret = -1;
	error_code = 0;
	p_stream = NULL;
	p_buf_head = NULL;
	flags = GET_FLAGS( buf );
	if( IS_CONTROL( buf ) ){
		p_u32 = ( uint32_t * )( buf + FRAME_HEAD_LEN );
		if( GET_VERSION( buf ) != SESSION_VERSION ){
			goto error;
		}	
		switch( GET_TYPE( buf ) ){
		case TYPE_SYN:
			if( len <= FRAME_SYN_HEAD_LEN ){
				MOON_PRINT_MAN( ERROR, "bad syn frame!" );
				goto error;
			}
			convert_uint32_t( p_u32 );
			stream_id = GET_STREAM_ID( p_u32 );
			p_u32++;
			convert_uint32_t( p_u32 );
			assoc_id = GET_STREAM_ID( p_u32 );
			p_u32++;
			convert_uint32_t( p_u32 );
			pri = GET_STREAM_PRI( p_u32 );
			if( ( p_stream = get_stream_ref( p_session, stream_id ) ) != NULL ){
				MOON_PRINT_MAN( ERROR, "syn frame: stream already exist!" );
				inter_error_stream( p_stream, STREAM_IN_USE );
				goto error;
			}
			if( ( stream_id % 2 ) != p_session->is_server 
				|| stream_id <= p_session->last_recv_stream_id ){
				MOON_PRINT_MAN( ERROR, "syn frame: stream id error!!" );
				add_rst_packet_to_session( p_session, stream_id, PROTOCOL_ERROR, USER_PRI1 );
				goto error;
			}
			p_session->last_recv_stream_id = stream_id;
			if( assoc_id != 0 ){//pushed stream
				if( ( assoc_id % 2 ) == p_session->is_server ){
					MOON_PRINT_MAN( ERROR, "syn frame: assoc id is invalid!" );
					add_rst_packet_to_session( p_session, stream_id, PROTOCOL_ERROR, USER_PRI1 );
					goto error;
				}
				p_assoc_stream = get_stream_ref( p_session, assoc_id );
				if( p_assoc_stream == NULL ){
					MOON_PRINT_MAN( ERROR, "syn frame: can't find stream with associated id" );
					add_rst_packet_to_session( p_session, stream_id, PROTOCOL_ERROR, USER_PRI1 );
					goto error;
				}
				if( p_assoc_stream->is_pushed != 0 ){
					MOON_PRINT_MAN( ERROR, "assoc stream is pushed stream!" );
					stream_ref_dec( p_assoc_stream );
					add_rst_packet_to_session( p_session, stream_id, INTERNAL_ERROR, USER_PRI1 );
					goto error;
				}
				p_stream = init_new_stream( stream_id, pri, 1, INITIAL_WINDOW_SIZE );
				if( p_stream == NULL ){
					MOON_PRINT_MAN( ERROR, "can't create new stream!" );
					stream_ref_dec( p_assoc_stream );
					add_rst_packet_to_session( p_session, stream_id, INTERNAL_ERROR, USER_PRI1 );
					goto error;
				}
				if( linked_stream_and_session( p_stream, p_session ) < 0 
					|| linked_pushed_and_main_stream( p_stream, p_assoc_stream ) < 0 ){
					MOON_PRINT_MAN( ERROR, "linked stream error!" );
					stream_ref_dec( p_assoc_stream );
					inter_error_stream( p_stream, INTERNAL_ERROR );
					goto error;
				}
				goto back;
			}else{//normal stream
				p_stream = init_new_stream( stream_id, pri, 0, INITIAL_WINDOW_SIZE );
				if( p_stream == NULL ){
					MOON_PRINT_MAN( ERROR, "can't create new stream!" );
					add_rst_packet_to_session( p_session, stream_id, REFUSED_STREAM, USER_PRI1 );
					goto error;
				}
				if( linked_stream_and_session( p_stream, p_session ) < 0 ){
					MOON_PRINT_MAN( ERROR, "linked stream error!" );
					inter_error_stream( p_stream, INTERNAL_ERROR );
					goto error;
				}
			}
			p_buf_head = packet_recv_packet( buf, len, FRAME_SYN_HEAD_LEN );
			if( p_buf_head == NULL ){
				inter_error_stream( p_stream, INTERNAL_ERROR );
				goto error;
			}
			error_code = inter_add_syn_packet_to_stream( p_stream, p_buf_head );
			if( error_code != 0 ){
				inter_error_stream( p_stream, error_code );
				goto error;
			}
			goto back;
			break;
		case TYPE_SYN_REPLY:
			if( len <= FRAME_SYN_REPLY_HEAD_LEN ){
				MOON_PRINT_MAN( ERROR, "packet too short!" );
				goto error;
			}
			convert_uint32_t( p_u32 );
			stream_id = GET_STREAM_ID( p_u32 );
			p_stream = get_stream_ref( p_session, stream_id );
			if( p_stream == NULL ){
				MOON_PRINT_MAN( ERROR, "can't find stream" );
				add_rst_packet_to_session( p_session, stream_id, PROTOCOL_ERROR, USER_PRI1 );
				goto error;
			}
			p_buf_head = packet_recv_packet( buf, len, FRAME_SYN_REPLY_HEAD_LEN );
			if( p_buf_head == NULL ){
				inter_error_stream( p_stream, INTERNAL_ERROR );
				goto error;
			}
			error_code = inter_add_synreply_packet_to_stream( p_stream, p_buf_head );
			if( error_code != 0 ){
				inter_error_stream( p_stream, INTERNAL_ERROR );
				goto error;
			}
			goto back;
			break;
		case TYPE_RST:
			if( len < FRAME_RST_LEN ){
				MOON_PRINT_MAN( ERROR, "packet too short!" );
				goto error;
			}
			convert_uint32_t( p_u32 );
			stream_id = GET_STREAM_ID( p_u32 );
			p_u32++;
			convert_uint32_t( p_u32 );
			status_code = *p_u32;
			p_stream = get_stream_ref( stream_id );
			if( p_stream == NULL ){
				goto error;
			}
			MOON_PRINT_MAN( WARNNING, "stream %d is closed %d", stream_id, status_code );
			inter_close_stream( p_stream, error_code );
			goto error;
			break;
		case TYPE_WINDOW_UPDATE:
			if( len < FRAME_WINDOW_UPDATE_LEN ){
				MOON_PRINT_MAN( ERROR, "packet too short!" );
				goto error;
			}
			convert_uint32_t( p_u32 );
			stream_id = GET_STREAM_ID( p_u32 );
			p_u32++;
			convert_uint32_t( p_u32 );
			delta_window_size = GET_STREAM_ID( p_u32 );
			p_stream = get_stream_ref( stream_id );
			if( p_stream == NULL ){
				MOON_PRINT_MAN( ERROR, "can't find stream" );
				add_rst_packet_to_session( p_session, stream_id, PROTOCOL_ERROR, USER_PRI1 );
				goto error;
			}
			loop_send_packet( p_stream, delta_window_size );
			goto error;
			break;
		case TYPE_GOAWAY:
			if( len < FRAME_GOAWAY_LEN ){
				MOON_PRINT_MAN( ERROR, "frame too short!" );
				goto error;
			}
			convert_uint32_t( p_u32 );
			last_recv_stream_idp = GET_STREAM_ID( p_u32 );
			p_u32++;
			convert_uint32_t( p_u32 );
			status_code = *p_u32;
			close_session( p_session );
			goto error;
			break;
		case TYPE_PING:
			int is_ping_ack = 0;
			unsigned long long ping_timer;
			timer_desc_s timer;
			if( len < FRAME_PING_LEN ){
				MOON_PRINT_MAN( ERROR, "frame too short!" );
				goto error;
			}
			convert_uint32_t( p_u32 );
			ping_id = *p_u32;
			if( ( ping_id % 2 ) == p_session->is_server ){//ping syn,just ack
				if( add_ping_packet_to_session( p_session, ping_id ) < 0 ){
					MOON_PRINT_MAN( ERROR, "ack ping error!" );
				}
			}else{//ping ack
				pthread_mutex_lock( &p_session->op_mutex );
				if( ping_id == p_session->last_ping_unique_id ){
					p_session->last_ping_unique_id = 0;
					ping_timer = p_session->ping_timer;
					p_session->ping_timer = 0;
					is_ping_ack = 1;
				}
				pthread_mutex_unlock( &p_session->op_mutex );
				if( is_ping_ack ){
					if( ping_timer != 0 ){
						timer_del( ping_timer );
					}
					session_ref_inc( p_session );
					timer.time_ms = PING_INTERNAL;
					timer.func = ping_send_timer_func;
					timer.free = ping_timer_free;
					timer.user_data.ptr = p_session;
					if( timer_add( &timer, &ping_timer, 0 ) < 0 ){
						MOON_PRINT_MAN( ERROR, "fatal error: add ping timer error!" );
						session_ref_dec( p_session );
						goto error;
					}
					pthread_mutex_lock( &p_session->op_mutex );
					p_session->ping_timer = ping_timer;
					pthread_mutex_unlock( &p_session->op_mutex );
				}
			}
			goto error;
			break;
		defaullt:
			MOON_PRINT_MAN( ERROR, "no such type packet!" );
			goto error;
		}
	}else{
		if( len <= FRAME_HEAD_LEN ){
			MOON_PRINT_MAN( ERROR, "data packet too short!" );
			goto error;
		}
		stream_id = GET_STREAM_ID( buf );
		p_stream = get_stream_ref( p_session, stream_id );
		if( p_stream == NULL ){
			MOON_PRINT_MAN( ERROR, "error: a packet belongs to none stream!" );
			add_rst_packet_to_session( p_session, stream_id, PROTOCOL_ERROR, USER_PRI1 );
			goto error;
		}
		p_buf_head = packet_recv_packet( buf, len, FRAME_HEAD_LEN );
		if( p_buf_head == NULL ){
			inter_error_stream( p_stream, INTERNAL_ERROR );
			goto error;
		}
		error_code = inter_add_data_packet_to_stream( p_stream, p_buf_head );
		if( error_code != 0 ){
			inter_error_stream( p_stream, INTERNAL_ERROR );
			goto error;
		}
		goto back;
	}
error:
	if( p_buf_head != NULL ){
		free_total_packet( p_buf_head );
	}else{
		packet_buf_free( buf );
	}
back:
	if( p_stream != NULL ){
		stream_ref_dec( p_stream );
	}
	return ret;
}

//在线程里独立运行
static int session_recv( common_user_data p_user_data )
{
	session p_session;
	socket_desc p_desc;
	active_input_packet p_aip;
	int ret, need_read, read_again;
	char * p_read;
	char tmp_buf[ FRAME_DROP_BUFFER_SIZE ];

	p_session = ( session )p_user_data->ptr;
	p_desc = p_session->p_desc;
	p_aip = &p_session->aip;
read_start:
	while( ( p_session->status & FD_GOAWAY_RECV ) == 0 ){
		if( p_aip->cur_head_in < FRAME_HEAD_LEN ){
			p_read = p_aip->headbuf + p_aip->cur_head_in;
			need_read = FRAME_HEAD_LEN - p_aip->cur_head_in;
			ret = p_desc->recv( p_desc, p_read, need_read );
			if( ret == SOCKET_NO_RES ){
				break;
			}else if( ret < 0 ){
				MOON_PRINT_MAN( ERROR, "recv error!" );
				break;
			}
			p_aip->cur_head_in += ret;
			if( ret < need_read ){
				break;
			}else{
				convert_uint32_t( p_aip->headbuf );
				convert_uint32_t( ( ( uint32_t * )p_aip->headbuf ) + 1 );
				p_aip->buf_len = GET_LENGTH( p_aip->headbuf );
				if( p_aip->buf_len > INBOUND_BUFFER_LENGTH 
					|| ( p_aip->buf = packet_buf_malloc( p_aip->buf_len + FRAME_HEAD_LEN ) ) == NULL ){
					MOON_PRINT_MAN( WARNNING, "packet too big, will drop!" );
					p_aip->is_drop = 1;
				}else{
					//头部一起往后传
					p_aip->buf_len += FRAME_HEAD_LEN;
					p_aip->cur_buf_in = FRAME_HEAD_LEN;
					memcpy( p_aip->buf, p_aip->headbuf, FRAME_HEAD_LEN );
				}
			}
		}else{
			need_read = p_aip->buf_len - p_aip->cur_buf_in;
			if( need_read == 0 ){
				if( p_aip->is_drop == 0 ){
					process_recv_packet( p_session, p_aip->buf, p_aip->len );		
				}
				memset( p_aip, 0, sizeof( *p_aip ) );
			}else{
				if( p_aip->is_drop == 0 ){
					p_read = p_aip->buf + p_aip->cur_buf_in;
				}else{
					p_read = tmp_buf;
					need_read = MIN( need_read, FRAME_DROP_BUFFER_SIZE );
				}
				ret = p_desc->recv( p_desc, p_read, need_read );
				if( ret == SOCKET_NO_RES ){
					break;
				}else if( ret < 0 ){
					MOON_PRINT_MAN( ERROR, "recv packet error!" );
					break;
				}else{
					p_aip->cur_buf_in += need_read;
				}
			}
		}
	}
	//查看是否又可以读了
	pthread_mutex_lock( &p_session->op_mutex );
	read_again = p_session->status & FD_CAN_RECV;
	p_session->status &= ~( FD_CAN_RECV | FD_RECVING );
	p_session->status |= FD_RECVING * ( read_again / FD_CAN_RECV );
	pthread_mutex_unlock( &p_session->op_mutex );
	if( read_again ){
		goto read_start;
	}
	session_ref_dec( p_session );
	return 0;
}

static int session_recv_event( socket_user_desc p_user )
{
	session p_session;
	int cant_read, error;
	process_task_s task;

	p_session = ( session )p_user->p_entity_data;
	cant_read = 1;
	pthread_mutex_lock( &p_session->op_mutex );
	if( !IS_SESSION_CLOSED( p_session ) ){
		cant_read = p_session->status & FD_RECVING;
		p_session->status |= FD_CAN_RECV * ( cant_read / FD_RECVING );
		p_session->status |= FD_RECVING;
	}
	pthread_mutex_unlock( &p_session->op_mutex );
	if( cant_read == 0 ){
		start_rs_task( p_session, FD_RECVING );
	}
	return 0;
}

static int session_send_event( socket_user_desc p_user )
{
	session p_session;
	int can_send, error;

	can_send = -1;
	p_session = ( session )p_user->p_entity_data;
	pthread_mutex_lock( &p_session->op_mutex );
	if( !IS_SESSION_CLOSED( p_session ) ){
		p_session->status |= FD_CAN_SEND;
		can_send = try_start_send_task( p_session );
	}
	pthread_mutex_unlock( &p_session->op_mutex ):
	if( can_send >= 0 ){
		start_rs_task( p_session, FD_SENDING );
	}
	return 0;
}

static int session_close_event( socket_user_desc p_user )
{
	session p_session;
	
	p_session = ( session )p_user->p_entity_data;
	close_session( p_session );
	return 0;
}

static socket_desc session_get_socket_desc( socket_user_desc p_user )
{
	session p_session;

	p_session = ( session )p_user->p_entity_data;
	return p_session->p_desc;
}

static void session_dec_ref( socket_user_desc p_user )
{
	session_ref_dec( ( session )p_user->p_entity_data );
}

static void session_inc_ref( socket_user_desc p_user )
{
	session_ref_inc( ( session )p_user->p_entity_data );
}

static inline void session_leave_listen( session p_session )
{
	if( remove_user_from_listen( ( socket_user_desc )p_session - 1 ) < 0 ){
		MOON_PRINT_MAN( ERROR, "remove from listen error!" );
	}
}

static inline void session_leave_hash_table( session p_session )
{
	int is_del;

	pthread_mutex_lock( &session_mutex  );
	is_del = is_hash_really_del( ( socket_user_desc )p_session - 1 );
	pthread_mutex_unlock( &session_mutex );
	if( is_del ){
		session_ref_dec( p_session );
	}
}

//先加入hash表，在注册监听，目前他们分配在一块内存上
//ret:-1 error,0 already exist, 1 new
static int add_session( session p_session, char * name, session * pp_session )
{
	socket_user_desc p_user;
	session p_new_session;
	int ret, is_close;

	pthread_mutex_lock( &session_mutex );
	p_user = hash_search( session_list, name, sizeof( socket_user_desc_s ) + sizeof( session_s ) );
	if( p_user == NULL ){//error
		MOON_PRINT_MAN( ERROR, "create new session error!" );
		ret = -1;
	}else if( p_user->p_entity_data != NULL ){//already exist
		session_ref_inc( ( session )( p_user + 1 ) );
		*pp_session = ( session )( p_user + 1 );
		ret = 0;
	}else{//new
		ret = 1;
		p_new_session = ( session )( p_user + 1 );
		*p_new_session = *p_session;
		p_user->send_event = session_send_event;
		p_user->recv_event = session_recv_event;
		p_user->close_event = session_close_event;
		p_user->get_socket_desc= session_get_socket_desc;
		p_user->dec_ref = session_dec_ref;
		p_user->inc_ref = session_inc_ref;
		p_user->p_entity_data = p_new_session;
		session_ref_inc( p_new_session );
	}
	pthread_mutex_unlock( &session_mutex );
	if( ret <= 0 ){
		return ret;
	}
	if( add_user_to_listen( p_user ) < 0 ){
		close_session( p_new_session );
		session_ref_dec( p_new_session );
		MOON_PRINT_MAN( ERROR, "add session to listen error!" );
		return -1;
	}
	//防止在加入hash表和加入监听之间session关闭，造成资源不能释放
	pthread_mutex_lock( &p_new_session->op_mutex );
	is_close = IS_SESSION_CLOSED( p_new_session );
	pthread_mutex_unlock( &p_new_session->op_mutex );
	if( is_close ){
		MOON_PRINT_MAN( WARNNING, "may be before add to liten, session is closed!" );
		session_leave_listen( p_new_session );
		session_ref_dec( p_new_session );
		return -1;
	}
	*pp_session = p_new_session;
	return ret;
}


