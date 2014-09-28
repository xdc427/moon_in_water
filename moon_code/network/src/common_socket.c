#include<stdio.h>
#include<unistd.h>
#include<pthread.h>
#include<stdlib.h>
#include<string.h>
#include<time.h>
#include<fcntl.h>
#include<errno.h>
#include<netdb.h>
#include<signal.h>
#include<sys/socket.h>
#include<sys/epoll.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/types.h>
#include"moon_debug.h"
#include"moon_pipe.h"
#include"common_socket.h"
#include"common_interfaces.h"
#include"moon_thread_info.h"

#ifdef MODENAME
#undef MODENAME
#endif
#define MODENAME "common_socket"

//分为三个层面：使用者（session），(本体（socket），驱动者（listen task）)这两者是內聚的

typedef enum{
	UNJOIN_SOCKET,//未加入epoll
	JOIN_SOCKET,//加入epoll
	CONNECT_SOCKET,
	LISTEN_SOCKET,
	NO_SOCKET
} fd_cat_e;

typedef struct{
	int status;
	int epoll_fd;
	int max_socket_num;//<0,则为无限制
	int cur_socket_num;
	list p_dels;
	pthread_t listen_task;
	pthread_mutex_t mutex;
} socket_pool_s;
typedef socket_pool_s * socket_pool;

//socket_private_s + list_s
typedef struct{
	int status;
	int socket_fd;
	socket_pool p_spool;
	fd_cat_e fd_cat;
	pipe_interface p_pipe_i;
	io_listener_interface p_io_listener_i;
	struct sockaddr_storage addr;
} socket_private_s;
typedef socket_private_s * socket_private;

void * get_socketpool_instance();
static void get_pipe_data_len( void * p_data, int * p_len );
static int new_socket( void * p_data, void * p_pipe, const char * ip, const char * port );
static int new_listen_socket( void * p_data, void * p_pipe, const char * port );
static void socket_close( void * p_data, void * p_pipe );
static int socket_read( void * p_data, void * p_pipe, char * buf, int len, int flags, ... );
static int socket_write( void * p_data, void * p_pipe, char * buf, int len, int flags, ... );
static int socket_control( void * p_data, void * p_pipe, int cmd, ... );
static void * listen_task( void * arg );
#ifdef MOON_TEST
static const char socket_seq_xid[] = "socket_seq";
static void free_echo( void * p_data );
#endif

//socket_pool interfaces
STATIC_BEGAIN_INTERFACE( socketpool_hub )
STATIC_DECLARE_INTERFACE( socketpool_interface_s )
STATIC_DECLARE_INTERFACE( pipe_listener_interface_s )
STATIC_END_DECLARE_INTERFACE( socketpool_hub, 2, socket_pool_s spool )
STATIC_GET_INTERFACE( socketpool_hub, socketpool_interface_s, 0 ) = {
	.new_listen_socket = new_listen_socket,
	.new_socket = new_socket
}
STATIC_GET_INTERFACE( socketpool_hub, pipe_listener_interface_s, 1 ) = {
	.get_pipe_data_len = get_pipe_data_len,
	.close = socket_close
#ifdef MOON_TEST
	,.free_pipe_data = free_echo
#endif
}
STATIC_INIT_USERDATA( spool ) = {
	.max_socket_num = -1,
	.mutex = PTHREAD_MUTEX_INITIALIZER
}
STATIC_END_INTERFACE( NULL )

//socket interfaces
STATIC_BEGAIN_INTERFACE( socket_hub )
STATIC_DECLARE_INTERFACE( io_pipe_interface_s )
STATIC_DECLARE_INTERFACE( pipe_listener_interface_s )
STATIC_END_DECLARE_INTERFACE( socket_hub, 2 )
STATIC_GET_INTERFACE( socket_hub, io_pipe_interface_s, 0 ) = {
	.read = socket_read,
	.write = socket_write,
	.close = socket_close,
	.control = socket_control
}
STATIC_GET_INTERFACE( socket_hub, pipe_listener_interface_s, 1 ) ={
	.close = socket_close
#ifdef MOON_TEST
	,.free_pipe_data = free_echo
#endif
}
STATIC_END_INTERFACE( NULL )

#ifdef MOON_TEST
static void free_echo( void * p_data )
{
	MOON_PRINT( TEST, socket_seq_xid, "%p:free:1", &( ( socket_private )p_data )->status );
}
#endif

void * get_socketpool_instance()
{
	socket_pool p_spool;
	pthread_attr_t attr;
	int ret;

	p_spool = &socketpool_hub.spool;
	if( __builtin_expect( p_spool->status == 0, 0 ) ){
		pthread_mutex_lock( &p_spool->mutex );
		if( p_spool->status == 0 ){
			//初始化epoll
			p_spool->epoll_fd = epoll_create( 1000 );
			if( __builtin_expect( p_spool->epoll_fd < 0, 0 ) ){
				MOON_PRINT_MAN( ERROR, "create epoll error!" );
				goto epoll_create_error;
			}
			pthread_attr_init( &attr );
			pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
			ret = pthread_create( &p_spool->listen_task, &attr, listen_task, p_spool );
			pthread_attr_destroy( &attr );
			if( __builtin_expect( ret != 0, 0 ) ){
				MOON_PRINT_MAN( ERROR, "create listen task thread error" );
				goto pthread_error;
			}
			signal( SIGPIPE, SIG_IGN );
			p_spool->status = 1;
			goto back;
		}
pthread_error:
		close( p_spool->epoll_fd );
		p_spool->epoll_fd = -1;
epoll_create_error:
		p_spool = NULL;
back:
		pthread_mutex_unlock( &p_spool->mutex );
	}
	return p_spool;
}

static void get_pipe_data_len( void * p_data, int * p_len )
{
	if( p_len != NULL ){
		*p_len = sizeof( list_s ) + sizeof( socket_private_s );
	}
}

static int set_socket_nonblock( int sock )
{	
	int flags;
	flags = fcntl( sock, F_GETFL, 0 );
	if( flags < 0 ){
		MOON_PRINT_MAN( ERROR, "fcntl(F_GETFL) failed" );
		return -1;
	}
	if ( fcntl( sock, F_SETFL, flags | O_NONBLOCK ) < 0 ){
		MOON_PRINT_MAN( ERROR, "fcntl(F_SETFL) failed" );
		return -1;
	}
	return 0;
}

static void _socket_close( void * p_data )
{ 
	socket_private p_private;
	void * p_point, * p_point_data;
	int ret;
	struct epoll_event ev;
	list p_list, p_tmp;
	socket_pool p_spool;

	MOON_PRINT( TEST, NULL, "socket_close" );
	p_private = p_data;
	p_spool = p_private->p_spool;
	switch( p_private->fd_cat ){
	case JOIN_SOCKET:
	case UNJOIN_SOCKET:
//		ret = p_private->p_pipe_i->get_other_point_ref( p_private, &p_point, &p_point_data );
//		if( ret >= 0 ){
//			CALL_INTERFACE_FUNC( p_point, io_listener_interface_s, close_event, p_point_data );
//			CALL_INTERFACE_FUNC( p_point, gc_interface_s, ref_dec );
//			CALL_INTERFACE_FUNC( p_point_data, gc_interface_s, ref_dec );
//		}
	case LISTEN_SOCKET:
	case CONNECT_SOCKET:
		if( p_private->fd_cat != UNJOIN_SOCKET ){
			epoll_ctl( p_spool->epoll_fd, EPOLL_CTL_DEL, p_private->socket_fd, &ev );
			__sync_sub_and_fetch( &p_spool->cur_socket_num, 1 );
		}
		close( p_private->socket_fd );
	case NO_SOCKET:
		p_private->p_pipe_i->close( p_private );
		if( p_private->fd_cat == UNJOIN_SOCKET || p_private->fd_cat == NO_SOCKET ){
			CALL_INTERFACE_FUNC( p_private, gc_interface_s, ref_dec );
		}else{
			p_list = ( list )( p_private + 1 );
			do{
				p_tmp = p_spool->p_dels;
				p_list->next = p_tmp;
			}while( !__sync_bool_compare_and_swap( ( uintptr_t * )&p_spool->p_dels
				, ( uintptr_t )p_tmp, ( uintptr_t )p_list ) );
		}
		break;
	default:
		MOON_PRINT_MAN( ERROR, "no such socket type!" );
	}
}

static int _new_socket( socket_private p_private, int fd, struct sockaddr_storage * p_addr )
{
	void * p_new_pipe[ 2 ];
	void * p_point, * p_point_data;
	int len, ret;
	socket_private p_new_private;
	order_listener_interface p_order_i;
	pipe_interface p_pipe_i;

	len = 0;
	p_pipe_i = p_private->p_pipe_i;
	ret = set_socket_nonblock( fd );
	if( __builtin_expect( ret < 0, 0 ) ){
		MOON_PRINT_MAN( ERROR, "set nio error!" );
		goto back;
	}
	ret = p_pipe_i->get_other_point_ref( p_private, &p_point, &p_point_data );
	if( __builtin_expect( ret < 0, 0 ) ){
		MOON_PRINT_MAN( ERROR, "get other point ref error!" );
		goto back;
	}
	CALL_INTERFACE_FUNC( p_point, pipe_listener_interface_s, get_pipe_data_len, &len );
	ret = pipe_new( p_new_pipe, sizeof( list_s ) + sizeof( socket_private_s ), len, 1 );
	if( __builtin_expect( ret < 0, 0 ) ){
		MOON_PRINT_MAN( ERROR, "pipe new error!" );
		goto free_pipe_data;
	}

	p_new_private = p_new_pipe[ 0 ];
	p_new_private->socket_fd = fd;
	p_new_private->addr = *p_addr;
	p_new_private->p_spool = p_private->p_spool;
	p_new_private->fd_cat = UNJOIN_SOCKET;
	p_new_private->p_pipe_i = FIND_INTERFACE( p_new_private, pipe_interface_s );

	p_pipe_i = p_new_private->p_pipe_i;
	p_pipe_i->set_point_ref( p_new_pipe[ 0 ], &socket_hub + 1 );
	p_order_i = FIND_INTERFACE( p_point, order_listener_interface_s );
	MOON_PRINT( TEST, socket_seq_xid, "%p:new:1", &p_new_private->status );
	ret = p_order_i->on_ready( p_point, p_point_data, p_new_pipe[ 1 ] );
	if( __builtin_expect( ret < 0 , 0 ) ){
		MOON_PRINT_MAN( ERROR, "order ready error!" );
		goto order_error;
	}
	ret = 0;
	goto free_pipe_data;

order_error:
	set_status_closed( &p_new_private->status, _socket_close, p_new_private );
free_pipe_data:
	CALL_INTERFACE_FUNC( p_point_data, gc_interface_s, ref_dec );
	CALL_INTERFACE_FUNC( p_point, gc_interface_s, ref_dec );
back:
	return ret;
}

static int new_socket( void * p_data, void * p_pipe, const char * ip, const char * port )
{
	socket_pool p_spool;
	socket_private p_private;
	struct addrinfo hints, * p_results;
	int new_fd, ret;
	pipe_interface p_pipe_i;
	struct epoll_event ev;

	ret = -1;
	if( __builtin_expect( p_data == NULL || p_pipe == NULL || ip == NULL || port == NULL, 0 ) ){
		if( p_pipe != NULL ){
			MOON_PRINT( TEST, socket_seq_xid, "%p:new:1", &( ( socket_private )p_pipe )->status );
			CALL_INTERFACE_FUNC( p_pipe, pipe_interface_s, init_done, 1 );
		}
		goto back;
	}
	p_spool = ( socket_pool )p_data;
	p_private = ( socket_private )p_pipe;

	p_pipe_i = FIND_INTERFACE( p_pipe, pipe_interface_s );
	p_pipe_i->set_point_ref( p_pipe, p_data );
	p_private->p_pipe_i = p_pipe_i;
	p_private->fd_cat = NO_SOCKET;
	p_private->p_spool = p_spool;

	CALL_INTERFACE_FUNC( p_private, gc_interface_s, ref_inc );
	MOON_PRINT( TEST, socket_seq_xid, "%p:new:1", &( ( socket_private )p_pipe )->status );
	p_pipe_i->init_done( p_pipe, 0 );

	memset( &hints, 0, sizeof( hints ) );
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICHOST;
	if( __builtin_expect( getaddrinfo( ip, port, &hints, &p_results ) != 0, 0 ) ){
		MOON_PRINT_MAN( ERROR, "get addr info error!" );
		goto addr_error;
	}
	memcpy( &p_private->addr, p_results->ai_addr, p_results->ai_addrlen );
	new_fd = socket( p_results->ai_family, p_results->ai_socktype, p_results->ai_protocol );
	if( __builtin_expect( new_fd < 0, 0 ) ){
		MOON_PRINT_MAN( ERROR, "new sockert error" );
		goto socket_error;
	}
	if( __builtin_expect( set_socket_nonblock( new_fd ) < 0, 0 ) ){
		MOON_PRINT_MAN( ERROR, "set socket nio error!" );
		goto set_error;
	}
	ret = connect( new_fd, p_results->ai_addr, p_results->ai_addrlen );
	if( ret == -1 && errno == EINPROGRESS ){//join epoll
		ev.events = EPOLLOUT | EPOLLET;
		ev.data.ptr = p_private;
		ret = useing_ref_inc( &p_private->status );
		if( __builtin_expect( ret < 0, 0 ) ){
			goto set_error;
		}
		p_private->socket_fd = new_fd;
		p_private->fd_cat = CONNECT_SOCKET;
		if( epoll_ctl( p_spool->epoll_fd, EPOLL_CTL_ADD, p_private->socket_fd, &ev ) == 0 ){
			__sync_add_and_fetch( &p_spool->cur_socket_num, 1 );
		}else{
			MOON_PRINT_MAN( ERROR, "add socket to epoll error!" );
			p_private->fd_cat = NO_SOCKET;
			useing_ref_dec( &p_private->status, _socket_close, p_private );
			goto set_error;
		}
		useing_ref_dec( &p_private->status, _socket_close, p_private );
		ret = 0;
	}else if( ret == 0 ){//success
		ret = _new_socket( p_private, new_fd, &p_private->addr );
		goto socket_error;
	}else{
		MOON_PRINT_MAN( ERROR, "connect error%d", errno );
	}
	freeaddrinfo( p_results );
	goto back;

set_error:
	close( new_fd );
socket_error:
	freeaddrinfo( p_results );
addr_error:
	set_status_closed( &p_private->status, _socket_close, p_private );
back:
	return ret;
}

static int new_listen_socket( void * p_data, void * p_pipe, const char * port )
{
	socket_pool p_spool;
	socket_private p_private;
	struct addrinfo hints, * p_results;
	int new_fd, ret, reuse;
	pipe_interface p_pipe_i;
	struct epoll_event ev;

	ret = -1;
	reuse = 1;
	if( __builtin_expect( p_data == NULL || p_pipe == NULL || port == NULL, 0 ) ){
		if( p_pipe != NULL ){
			MOON_PRINT( TEST, socket_seq_xid, "%p:new:1", &( ( socket_private )p_pipe )->status );
			CALL_INTERFACE_FUNC( p_pipe, pipe_interface_s, init_done, 1 );
		}
		goto back;
	}
	p_spool = ( socket_pool )p_data;
	p_private = ( socket_private )p_pipe;

	p_pipe_i = FIND_INTERFACE( p_pipe, pipe_interface_s );
	p_pipe_i->set_point_ref( p_pipe, p_data );
	p_private->p_pipe_i = p_pipe_i;
	p_private->fd_cat = NO_SOCKET;
	p_private->p_spool = p_spool;
	CALL_INTERFACE_FUNC( p_private, gc_interface_s, ref_inc );
	MOON_PRINT( TEST, socket_seq_xid, "%p:new:1", &( ( socket_private )p_pipe )->status );
	p_pipe_i->init_done( p_pipe, 0 );

	memset( &hints, 0, sizeof( hints ) );
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if( __builtin_expect( getaddrinfo( NULL, port, &hints, &p_results ) != 0, 0 ) ){
		MOON_PRINT_MAN( ERROR, "get addr info error!" );
		goto addr_error;
	}
	memcpy( &p_private->addr, p_results->ai_addr, p_results->ai_addrlen );
	new_fd = socket( p_results->ai_family, p_results->ai_socktype, p_results->ai_protocol );
	if( __builtin_expect( new_fd < 0, 0 ) ){
		MOON_PRINT_MAN( ERROR, "new sockert error" );
		goto socket_error;
	}
	if( setsockopt( new_fd, SOL_SOCKET, SO_REUSEADDR, ( char * )&reuse, sizeof( reuse ) ) < 0
		|| set_socket_nonblock( new_fd ) < 0 
		|| bind( new_fd, p_results->ai_addr, p_results->ai_addrlen ) < 0 
		|| listen( new_fd , 800 ) < 0 ){
		MOON_PRINT_MAN( ERROR, "bind or listen error!" );
		goto set_error;
	}

	ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
	ev.data.ptr = p_private;
	ret = useing_ref_inc( &p_private->status );
	if( __builtin_expect( ret < 0, 0 ) ){
		goto set_error;
	}
	p_private->socket_fd = new_fd;
	p_private->fd_cat = LISTEN_SOCKET;
	if( epoll_ctl( p_spool->epoll_fd, EPOLL_CTL_ADD, p_private->socket_fd, &ev ) == 0 ){
		__sync_add_and_fetch( &p_spool->cur_socket_num, 1 );
	}else{
		MOON_PRINT_MAN( ERROR, "add socket to epoll error!" );
		p_private->fd_cat = NO_SOCKET;
		useing_ref_dec( &p_private->status, _socket_close, p_private );
		goto set_error;
	}
	useing_ref_dec( &p_private->status, _socket_close, p_private );
	freeaddrinfo( p_results );
	ret = 0;
	goto back;

set_error:
	close( new_fd );
socket_error:
	freeaddrinfo( p_results );
addr_error:
	set_status_closed( &p_private->status, _socket_close, p_private );
back:
	return ret;
}

static void _socket_can_read( socket_private p_private )
{
	void * p_point, * p_point_data;
	int ret;
	socklen_t addr_len;
	struct sockaddr_storage addr;
	common_user_data_u user_data;

	switch( p_private->fd_cat ){
	case JOIN_SOCKET:
		MOON_PRINT( TEST, NULL, "read_pfm_before" );
		ret = p_private->p_pipe_i->get_other_point_ref( p_private, &p_point, &p_point_data );
		if( ret >= 0 ){
			p_private->p_io_listener_i->recv_event( p_point, p_point_data, user_data );
			CALL_INTERFACE_FUNC( p_point, gc_interface_s, ref_dec );
			CALL_INTERFACE_FUNC( p_point_data, gc_interface_s, ref_dec );
		}else{
			MOON_PRINT( TEST, NULL, "setclosed_pfm_before" );
			set_status_closed( &p_private->status, _socket_close, p_private );
			MOON_PRINT( TEST, NULL, "setclosed_pfm_end" );
		}
		MOON_PRINT( TEST, NULL, "read_pfm_end" );
		break;
	case LISTEN_SOCKET:
		ret = useing_ref_inc( &p_private->status );
		if( __builtin_expect( ret < 0, 0 ) ){
			set_status_closed( &p_private->status, _socket_close, p_private );
			break;
		}
		MOON_PRINT( TEST, NULL, "accept_pfm_before" );
		addr_len = sizeof( addr );
		while( 1 ){
			ret = accept( p_private->socket_fd, ( struct sockaddr * )&addr, &addr_len );
			if( ret >= 0 ){
				MOON_PRINT( TEST, NULL, "newsocket_pfm_before" );
				_new_socket( p_private, ret, &addr );
				MOON_PRINT( TEST, NULL, "newsocket_pfm_end" );
			}else{
				if( errno == EINTR ){//interrupt
					continue;
				}else if( errno != EAGAIN && errno != EWOULDBLOCK ){
					MOON_PRINT_MAN( ERROR, "accept error:%d", errno );
//					set_status_closed( &p_private->status, _socket_close, p_private );
				}
				break;
			}
		}
		useing_ref_dec( &p_private->status, _socket_close, p_private );
		MOON_PRINT( TEST, NULL, "accept_pfm_end" );
		break;
	default:
		MOON_PRINT_MAN( ERROR, "unkoow can read socket type:%d", p_private->fd_cat );
	}
}

static void _socket_can_write( socket_private p_private )
{
	void * p_point, * p_point_data;
	int ret, result;
	socklen_t result_len;
	struct epoll_event ev;
	common_user_data_u user_data;

	switch( p_private->fd_cat ){
	case JOIN_SOCKET:
		MOON_PRINT( TEST, NULL, "write_pfm_before" );
		ret = p_private->p_pipe_i->get_other_point_ref( p_private, &p_point, &p_point_data );
		if( ret >= 0 ){
			p_private->p_io_listener_i->send_event( p_point, p_point_data, user_data );
			CALL_INTERFACE_FUNC( p_point, gc_interface_s, ref_dec );
			CALL_INTERFACE_FUNC( p_point_data, gc_interface_s, ref_dec );
		}else{
			MOON_PRINT( TEST, NULL, "setclosed_pfm_before" );
			set_status_closed( &p_private->status, _socket_close, p_private );
			MOON_PRINT( TEST, NULL, "setclosed_pfm_end" );
		}
		MOON_PRINT( TEST, NULL, "write_pfm_end" );
		break;
	case CONNECT_SOCKET:
		result_len = sizeof( result );
		ret = useing_ref_inc( &p_private->status );
		if( __builtin_expect( ret < 0, 0 ) ){
			set_status_closed( &p_private->status, _socket_close, p_private );
			break;
		}
		MOON_PRINT( TEST, NULL, "connect_pfm_before" );
		ret = getsockopt( p_private->socket_fd, SOL_SOCKET, SO_ERROR, &result, &result_len );
		if( ret == 0 && result == 0 ){
			p_private->fd_cat = NO_SOCKET;
			epoll_ctl( p_private->p_spool->epoll_fd, EPOLL_CTL_DEL, p_private->socket_fd, &ev );
			MOON_PRINT( TEST, NULL, "newsocket_pfm_before" );
			_new_socket( p_private, p_private->socket_fd, &p_private->addr );
			MOON_PRINT( TEST, NULL, "newsocket_pfm_end" );
		}else{
			MOON_PRINT_MAN( ERROR, "connect error:%d", result );
		}
		useing_ref_dec( &p_private->status, _socket_close, p_private );
		set_status_closed( &p_private->status, _socket_close, p_private );
		MOON_PRINT( TEST, NULL, "connect_pfm_end" );
		break;
	default:
		MOON_PRINT_MAN( ERROR, "unkoow can write socket type:%d", p_private->fd_cat );
	}
}

static void socket_close( void * p_data, void * p_pipe )
{
	socket_private p_private;

	if( p_data != NULL && p_pipe != NULL ){
		p_private = p_pipe;
		set_status_closed( &p_private->status, _socket_close, p_private );
	}
}

static inline int _socket_rw( socket_private p_private, char * buf, int len, int is_recv )
{
	int already_rw, left, ret;

	if( buf == NULL || len <= 0 || p_private == NULL ){
		MOON_PRINT_MAN( ERROR, "input paraments error!" );
		return SOCKET_PARAERROR;
	}
	left = len;
	already_rw = 0;
	ret = useing_ref_inc( &p_private->status );
	if( __builtin_expect( ret < 0, 0 ) ){
		return SOCKET_CLOSED;
	}
	while( left > 0 ){
		if( is_recv ){
			ret = recv( p_private->socket_fd, buf + already_rw, left, 0 );
		}else{
			ret = send( p_private->socket_fd, buf + already_rw, left, 0 );
		}
		if( ret > 0 ){
			already_rw += ret;
			left -= ret;
		}else if( ret < 0 ){
			if( errno == EAGAIN || errno == EWOULDBLOCK ){
				ret = SOCKET_NO_RES;
			}else if( errno == EINTR ){
				continue;
			}else{
				ret = SOCKET_ERROR;
			}
			break;
		}else if( ret == 0 ){
			ret = SOCKET_CLOSED;
			break;
		}
	}
	useing_ref_dec( &p_private->status, _socket_close, p_private );
	if( already_rw > 0 ){
		return already_rw;
	}else{
		return ret;
	}
}

static int socket_read( void * p_data, void * p_pipe, char * buf, int len, int flags, ... )
{
	return _socket_rw( p_pipe, buf, len, 1 );
}

static int socket_write( void * p_data, void * p_pipe, char * buf, int len, int flags, ... )
{
	return _socket_rw( p_pipe, buf, len, 0 );
}

static int socket_control( void * p_data, void * p_pipe, int cmd, ... )
{
	socket_private p_private;
	int tmp, ret;
	void * p_point, *p_point_data;
	struct epoll_event ev;

	if( p_data == NULL || p_pipe == NULL ){
		return SOCKET_PARAERROR;
	}
	p_private = p_pipe;
	switch( cmd ){
	case SOCKET_START:
		ret = useing_ref_inc( &p_private->status );
		if( ret >= 0 ){
			do{
				tmp = p_private->fd_cat;
			}while( tmp == UNJOIN_SOCKET
					&& !__sync_bool_compare_and_swap( &p_private->fd_cat, tmp, JOIN_SOCKET ) );
			if( tmp == UNJOIN_SOCKET ){
				ret = p_private->p_pipe_i->get_other_point_ref( p_private, &p_point, &p_point_data );
				if( ret >= 0 ){
					p_private->p_io_listener_i = FIND_INTERFACE( p_point, io_listener_interface_s );
					CALL_INTERFACE_FUNC( p_point, gc_interface_s, ref_dec );
					CALL_INTERFACE_FUNC( p_point_data, gc_interface_s, ref_dec );
				}
				ev.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
				ev.data.ptr = p_private;
				ret = epoll_ctl( p_private->p_spool->epoll_fd
					, EPOLL_CTL_ADD, p_private->socket_fd, &ev );
				if( ret >= 0 ){
					__sync_add_and_fetch( &p_private->p_spool->cur_socket_num, 1 );
				}else{
					MOON_PRINT_MAN( ERROR, "add to epoll error!" );
					p_private->fd_cat = UNJOIN_SOCKET;
				}
			}
			useing_ref_dec( &p_private->status, _socket_close, p_private );
		}
		if( ret < 0 ){
			ret = SOCKET_ERROR;
		}
		break;
	default:
		ret = SOCKET_ERROR;
		MOON_PRINT_MAN( ERROR, "unknown cmd" );
	}
	return ret;
}

enum{
	MAX_EVENTS = 64
};

static void * listen_task( void * arg )
{
	socket_pool p_spool;
	int i, nfds;
	struct epoll_event events[ MAX_EVENTS ];
	socket_private p_private;
	list p_list;
	thread_info p_info;
	
	if( ( p_info = init_thread() ) != NULL ){
		p_info->level = THREAD_LEVEL0;
	}
	p_spool = ( socket_pool )arg;
	for( ; ; ){
		nfds = epoll_wait( p_spool->epoll_fd, events, MAX_EVENTS, 100 );
		if( nfds < 0 ){
			MOON_PRINT_MAN( ERROR, "epoll wait error!" );
			usleep( 100000 );
			continue;
		}
		MOON_PRINT( TEST, NULL, "epoll_pfm_before" );
		for( i = 0; i < nfds; i++ ){
			p_private = events[ i ].data.ptr;
			if( __builtin_expect( p_private == NULL, 0 ) ){
				MOON_PRINT_MAN( ERROR, "fatal error:epoll events ptr is NULL" );
				continue;
			}
			if( events[ i ].events & EPOLLIN ){
				_socket_can_read( p_private );
			}
			if( events[ i ].events & EPOLLOUT ){
				_socket_can_write( p_private );
			}
			if( events[ i ].events & ( EPOLLRDHUP | EPOLLERR | EPOLLHUP ) ){//close
				set_status_closed( &p_private->status, _socket_close, p_private );
			}
		}
		MOON_PRINT( TEST, NULL, "epolldel_pfm_before" );
		do{
			p_list = p_spool->p_dels;
		}while( !__sync_bool_compare_and_swap( ( uintptr_t * )&p_spool->p_dels, ( uintptr_t )p_list, 0 ) );
		while( p_list != NULL ){
			p_private = ( socket_private )p_list -1;
			p_list = p_list->next;
			CALL_INTERFACE_FUNC( p_private, gc_interface_s, ref_dec );
		}
		MOON_PRINT( TEST, NULL, "epolldel_pfm_end" );
		MOON_PRINT( TEST, NULL, "epoll_pfm_end" );
	}
	return NULL;
}

