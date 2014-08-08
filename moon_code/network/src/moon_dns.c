#include<sys/time.h>
#include<sys/epoll.h>
#include<sys/socket.h>
#include<sys/eventfd.h>
#include<sys/timerfd.h>
#include<regex.h>
#include<arpa/inet.h>
#include<netinet/in.h>
#include<netdb.h>
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<pthread.h>
#include<ares.h>
#include<signal.h>
#include"moon_pipe.h"
#include"moon_dns.h"
#include"common_interfaces.h"

#undef MODENAME
#define MODENAME "moon_dns"

enum{
	DNS_TIMEOUT_S = 3600,
	DNS_CACHE_NUM = 1024
};

enum {
	//begain 0
	URL_SCHEAME_INDEX = 2,
	URL_HOST_DOMAIN_INDEX = 4,
	URL_HOST_IPV6_INDEX = 7,
	URL_PORT_INDEX = 9,
	URL_PATH_INDEX = 10,
	URL_MATCH_NUM
};

enum{
	DOMAIN_QUERYING = 1,
	DOMAIN_QUERY1,
	DOMAIN_QUERY2,
	DOMAIN_NEW_QUERYING,
	DOMAIN_NEW_QUERY1
};

//double_list_s + dns_query_s
typedef struct dns_query_s{
	int status;//!=0 已取出
	struct dns_domain_s * p_domain;
} dns_query_s;
typedef dns_query_s * dns_query;

typedef struct dns_addrs_s{
	int ref_num;
	int n;
	double_list_s dlist[ 2 ];
	addr_elem p_head;
	addr_elem p_tail;
	struct timeval tv;
} dns_addrs_s;
typedef dns_addrs_s * dns_addrs;

//double_list_s + dns_domain_s + list_s
typedef struct dns_domain_s{
	struct dns_private_s * p_dns;
	int status;//会进行两次查询：ipv4 和 ipv6
	dns_addrs p_addrs;
	dns_addrs p_new_addrs;
	dns_query p_query_head;
} dns_domain_s;
typedef dns_domain_s * dns_domain;

typedef struct dns_private_s{
	int status;	
	int epoll_fd;
	int event_fd;//事件与数据相分离
	ares_channel p_ares;
	char * url_regex_string;
	regex_t url_regex;
	double_list_s domain_table[ HASH_NUM ];
	double_list_s head_tail[ 2 ];
	dns_domain p_domain_head;//old list
	dns_domain p_domain_tail;
	int domain_num;
	pthread_mutex_t mutex;
	list p_add_cares;
	pthread_t dns_pt;
} dns_private_s;
typedef dns_private_s * dns_private;

static dns_addrs new_addrs( );
static int get_addrs_num( void * p_data );
static int get_addrs_timestap( void * p_data, struct timeval * p_tv );
static addr_elem get_addrs_next( void * p_data, addr_elem p_elem );
static addr_elem get_addrs_prev( void * p_data, addr_elem p_elem );
static void addrs_ref_inc( void * p_data );
static void addrs_ref_dec( void * p_data );

STATIC_BEGAIN_INTERFACE( dns_addrs_hub )
STATIC_DECLARE_INTERFACE( dns_addrs_interface_s )
STATIC_DECLARE_INTERFACE( gc_interface_s )
STATIC_END_DECLARE_INTERFACE( dns_addrs_hub, 2 )
STATIC_GET_INTERFACE( dns_addrs_hub, dns_addrs_interface_s, 0 ) = {
	
	.get_num = get_addrs_num,
	.get_timestamp = get_addrs_timestap,
	.get_next = get_addrs_next,
	.get_prev = get_addrs_prev
}
STATIC_GET_INTERFACE( dns_addrs_hub, gc_interface_s, 1 ) = {
	.ref_inc = addrs_ref_inc,
	.ref_dec = addrs_ref_dec
}
STATIC_END_INTERFACE( NULL )

static int analyse_url( void * p_data, const char * url, url_desc p_desc );
static void get_pipe_data_len( void * p_data, int * p_len );
static int moon_getaddrinfo( void * p_data, void * p_pipe, const char * domain, int flags, struct timeval *p_tv );
static void cancel_query( void * p_data, void * p_pipe );
void * dns_task( void * aMOON_PRINT_MANrg );

STATIC_BEGAIN_INTERFACE( dns_hub )
STATIC_DECLARE_INTERFACE( dns_interface_s )
STATIC_DECLARE_INTERFACE( pipe_listener_interface_s )
STATIC_END_DECLARE_INTERFACE( dns_hub, 2,  dns_private_s dns )
STATIC_GET_INTERFACE( dns_hub, dns_interface_s, 0 ) ={
	.analyse_url = analyse_url,
	.getaddrinfo = moon_getaddrinfo
}
STATIC_GET_INTERFACE( dns_hub, pipe_listener_interface_s, 1 ) ={
	.get_pipe_data_len = get_pipe_data_len,
	.close = cancel_query
}
STATIC_INIT_USERDATA( dns ) = {
	.url_regex_string = "^((\\w+)://)?(((\\w+\\.)+\\w+)|(\\[([0-9a-fA-F:.]+)\\]))(:([1-9][0-9]{0,4}))?(/.*)?$",
	.mutex = PTHREAD_MUTEX_INITIALIZER

}
STATIC_END_INTERFACE( NULL )

static int analyse_url( void * p_data, const char * url, url_desc p_desc )
{
	
	dns_private p_dns;
	int ret;
	regmatch_t match[ URL_MATCH_NUM ];
	char error_buf[ 64 ];

	ret = -1;
	if( p_data != NULL && url != NULL && p_desc != NULL ){
		p_dns = p_data;
		ret = regexec( &p_dns->url_regex, url, URL_MATCH_NUM, match, 0 );
		if( ret == 0 ){
			p_desc->scheame_match = match[ URL_SCHEAME_INDEX ];
			if( match[ URL_HOST_DOMAIN_INDEX ].rm_eo > match[ URL_HOST_DOMAIN_INDEX ].rm_so ){
				p_desc->domain_match = match[ URL_HOST_DOMAIN_INDEX ];
			}else{//ipv6
				p_desc->domain_match = match[ URL_HOST_IPV6_INDEX ];
			}
			p_desc->port_match = match[ URL_PORT_INDEX ];
			p_desc->path_match = match[ URL_PATH_INDEX ];
		}else{
			regerror( ret, &p_dns->url_regex, error_buf, sizeof( error_buf ) );
			MOON_PRINT_MAN( ERROR, "url regex exec error:%s", error_buf );
			ret = -1;
		}
	}
	return ret;
}

static int regex_init( dns_private p_dns )
{
	int ret;
	char error_buf[ 64 ];

	if( ( ret = regcomp( &p_dns->url_regex, p_dns->url_regex_string, REG_EXTENDED ) ) != 0 ){
		regerror( ret, &p_dns->url_regex, error_buf, sizeof( error_buf ) );
		MOON_PRINT_MAN( ERROR, "url regex compile error:%s", error_buf );
		return -1;
	}
	return 0;
}

static void  regex_free( dns_private p_dns )
{
	regfree( &p_dns->url_regex );
}

void * get_dnsinstance()
{
	dns_private p_dns;
	int ret;
	struct epoll_event ev;
	pthread_attr_t attr;

	p_dns = &dns_hub.dns;
	if( __builtin_expect( p_dns->status == 0, 0 ) ){
		pthread_mutex_lock( &p_dns->mutex );
		if( p_dns->status == 0 ){
			//init sock_pair
			p_dns->event_fd = eventfd( 0, EFD_NONBLOCK );
			if( __builtin_expect( p_dns->event_fd < 0, 0 ) ){
				MOON_PRINT_MAN( ERROR, "eventfd error!" );
				goto efd_error;
			}
			//init epoll
			p_dns->epoll_fd = epoll_create( 1000 );
			if( __builtin_expect( p_dns->epoll_fd <  0, 0 ) ){
				MOON_PRINT_MAN( ERROR, "epoll create error!" );
				goto epoll_error;
			}
			ev.data.fd = p_dns->event_fd;
			ev.events = EPOLLIN | EPOLLRDHUP;
			ret = epoll_ctl( p_dns->epoll_fd, EPOLL_CTL_ADD, ev.data.fd, &ev );
			if( __builtin_expect( ret < 0, 0 ) ){
				MOON_PRINT_MAN( ERROR, "add socketpair to epoll error!" );
				goto regex_error;
			}
			//init regex
			ret = regex_init( p_dns );
			if( __builtin_expect( ret < 0, 0 ) ){
				MOON_PRINT_MAN( ERROR, "regex init error!" );
				goto regex_error;
			}
			//start dns task
			pthread_attr_init( &attr );
			pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
			ret = pthread_create( &p_dns->dns_pt, &attr, dns_task, p_dns );
			pthread_attr_destroy( &attr );
			if( __builtin_expect( ret != 0, 0 ) ){
				MOON_PRINT_MAN( ERROR, "create dns task thread error" );
				goto pthread_error;
			}
			p_dns->p_domain_head = list_to_data( &p_dns->head_tail[ 0 ] );
			p_dns->p_domain_tail = list_to_data( &p_dns->head_tail[ 1 ] );
			dlist_append( p_dns->p_domain_head, p_dns->p_domain_tail );
			signal( SIGPIPE, SIG_IGN );
			p_dns->status = 1;
			goto back;
		}
pthread_error:
		regex_free( p_dns );
regex_error:
		close( p_dns->epoll_fd );
epoll_error:
		close( p_dns->event_fd );
efd_error:
		p_dns = NULL;
back:
		pthread_mutex_unlock( &p_dns->mutex );
	}
	return p_dns;
}

static void get_pipe_data_len( void * p_data, int * p_len )
{
	*p_len = sizeof( double_list_s ) + sizeof( dns_query_s );
}

static inline void add_domain_to_query( dns_domain p_domain )
{
	list p_list, p_tmp;
	dns_private p_dns;
	uint64_t u64;

	p_dns = p_domain->p_dns;
	p_list = ( list )( p_domain + 1 );
	do{
		p_tmp = p_dns->p_add_cares;
		p_list->next = p_tmp;
	}while( !__sync_bool_compare_and_swap( &p_dns->p_add_cares, p_tmp, p_list ) );
	u64 = 1;
	write( p_dns->event_fd, &u64, sizeof( u64 ) );
}

static inline int compare_tv( struct timeval * p_tv1, struct timeval * p_tv2 )
{

	if( p_tv1->tv_sec > p_tv2->tv_sec ){
		return 1;
	}else if( p_tv1->tv_sec < p_tv2->tv_sec ){
		return -1;
	}else if( p_tv1->tv_usec > p_tv2->tv_usec ){
		return 1;
	}else if( p_tv1->tv_usec < p_tv2->tv_usec ){
		return -1;
	}else{
		return 0;
	}
}

static void notice_user( dns_query p_query, dns_addrs p_addrs )
{
	void *p_point, * p_point_data;
	pipe_interface p_pipe_i;
	dns_listener_interface p_dns_listener_i;
	int ret;
	void * p_pipe;

	for( ; p_query != NULL; p_query = dlist_next( p_query ) ){
		p_pipe = data_to_list( p_query );
		p_pipe_i = FIND_INTERFACE( p_pipe, pipe_interface_s );
		if( p_addrs != NULL ){
			ret = p_pipe_i->get_other_point_ref( p_pipe, &p_point, &p_point_data );
			if( ret >= 0 ){
				p_dns_listener_i = FIND_INTERFACE( p_point, dns_listener_interface_s );
				p_dns_listener_i->on_getaddrinfo( p_point, p_point_data, p_addrs );
				CALL_INTERFACE_FUNC( p_point, gc_interface_s, ref_dec);
				CALL_INTERFACE_FUNC( p_point_data, gc_interface_s, ref_dec );
			}
		}
		p_pipe_i->close( p_pipe );
		CALL_INTERFACE_FUNC( p_pipe, gc_interface_s, ref_dec );
	}
	addrs_ref_dec( p_addrs );
}

static int moon_getaddrinfo( void * p_data, void * p_pipe, const char * domain, int flags, struct timeval *p_tv )
{
	dns_private p_dns;
	dns_query p_query;
	dns_domain p_domain;
	addr_elem_s addr, * p_elem;
	int ret, is_new, init_fail, add_query;
	pipe_interface p_pipe_i;
	dns_addrs p_new_addrs;

	if( p_data == NULL || p_pipe == NULL || domain == NULL ){
		if( p_pipe != NULL ){
			CALL_INTERFACE_FUNC( p_pipe, pipe_interface_s, init_done, 1 );
		}
		return -1;
	}
	p_new_addrs = NULL;
	add_query = is_new = 0;
	init_fail = 1;
	p_dns = p_data;
	p_query = list_to_data( p_pipe );
	p_pipe_i = FIND_INTERFACE( p_pipe, pipe_interface_s );
	addr.family = AF_INET;
	addr.len = 4;
	ret = inet_pton( addr.family, domain, addr.addr_buf );
	if( ret <= 0){
		addr.family = AF_INET6;
		addr.len = 16;
		ret = inet_pton( addr.family, domain, addr.addr_buf );
	}
	if( ret <= 0 ){
		MOON_PRINT_MAN( NORMAL, "not numerichost!" );
		pthread_mutex_lock( &p_dns->mutex );
		p_domain = list_to_data( hash_search( p_dns->domain_table, domain
					, sizeof( double_list_s ) + sizeof( dns_domain_s ) + sizeof( list_s ) ) );
		if( p_domain != NULL ){
			init_fail = 0;
			switch( p_domain->status ){
				case 0:
					p_domain->p_dns = p_dns;
					p_domain->status++;
					is_new =  1;
				case DOMAIN_QUERYING:
				case DOMAIN_QUERY1:
					add_query = 1;
					break;
				case DOMAIN_QUERY2:
				case DOMAIN_NEW_QUERYING:
				case DOMAIN_NEW_QUERY1:
					if( ( flags & DNS_FLAG_CLEAR_CACHE ) != 0 
						&& ( p_tv == NULL 
							|| compare_tv( &p_domain->p_addrs->tv, p_tv ) <= 0 ) ){
						is_new = p_domain->status == DOMAIN_QUERY2;
						p_domain->status = p_domain->status - DOMAIN_QUERY2 + is_new;
						dlist_del( p_domain );
						p_dns->domain_num--;
						add_query = 1;
					}else if( ( flags & DNS_FLAG_DONT_USE_CHACHE ) != 0 
							&& ( p_tv == NULL 
								|| compare_tv( &p_domain->p_addrs->tv, p_tv ) <= 0 ) ){
						is_new = p_domain->status == DOMAIN_QUERY2;
						p_domain->status += is_new;
						add_query = 1;
					}else{
						addrs_ref_inc( p_domain->p_addrs );
						p_new_addrs = p_domain->p_addrs;
					}
				default:
					MOON_PRINT_MAN( ERROR, "domain status error!" );
			}
			if( add_query ){
				CALL_INTERFACE_FUNC( p_pipe, gc_interface_s, ref_inc );
				p_domain->p_query_head = dlist_insert( p_domain->p_query_head, p_query );
				p_query->p_domain = p_domain;
				p_pipe_i->set_point_ref( p_pipe, p_dns );
			}
		}
		pthread_mutex_unlock( &p_dns->mutex );
		p_query->status = add_query ^ 1;	
	}else{
		p_query->status = 1;
		p_new_addrs = new_addrs();
		p_elem = dlist_malloc( sizeof( *p_elem ) );
		if( p_new_addrs != NULL && p_elem != NULL){
			init_fail = 0;
			p_new_addrs->n = 1;
			gettimeofday( &p_new_addrs->tv, NULL );
			memcpy( p_elem, &addr, sizeof( *p_elem ) );
			dlist_append( p_new_addrs->p_head, p_elem );
		}else if( p_new_addrs != NULL ){
			addrs_ref_dec( p_new_addrs );
			p_new_addrs = NULL;
		}
	}
	p_pipe_i->init_done( p_pipe, init_fail );
	if( p_new_addrs != NULL ){
		CALL_INTERFACE_FUNC( p_pipe, gc_interface_s, ref_inc );
		notice_user( p_query, p_new_addrs );
	}else if( is_new ){
		add_domain_to_query( p_domain );
	}
	return -init_fail;
}

static void cancel_query( void * p_data, void * p_pipe )
{
	dns_private p_dns;
	dns_query p_query;
	int is_del;
	
	is_del = 0;
	if( p_data != NULL && p_pipe != NULL ){
		p_dns = p_data;
		p_query = list_to_data( p_pipe );
		if( p_query->status != 0 ){
			return;
		}
		pthread_mutex_lock( &p_dns->mutex );
		if( p_query->status == 0 ){
			if( p_query->p_domain->p_query_head == p_query ){
				p_query->p_domain->p_query_head = dlist_del( p_query );
			}else{
				dlist_del( p_query );
			}
			is_del = p_query->status = 1;
		}
		pthread_mutex_unlock( &p_dns->mutex );
		if( is_del ){
			CALL_INTERFACE_FUNC( p_pipe, gc_interface_s, ref_dec );
		}
	}
}

static inline dns_addrs merge_addrs( dns_domain p_domain, dns_addrs p_addrs )
{
	int n;
	dns_addrs p_addrs_del;
	addr_elem p_elem_head, p_elem_tail, p_elem;
	
	p_addrs_del = p_domain->p_addrs;
	if( p_addrs != NULL && p_domain->p_new_addrs != NULL ){
		n = p_addrs->n + p_domain->p_new_addrs->n;
		p_elem_head = dlist_next( p_addrs->p_head );
		p_elem_tail = dlist_prev( p_addrs->p_tail );
		dlist_del( p_addrs->p_head );
		dlist_del( p_addrs->p_tail );
		dlist_append( p_addrs->p_head, p_addrs->p_tail );
		p_addrs->n = 0;
		p_elem = dlist_prev( p_domain->p_new_addrs->p_tail );
		dlist_del( p_domain->p_new_addrs->p_tail );
		dlist_append( p_elem, p_elem_head );
		dlist_insert( p_domain->p_new_addrs->p_tail, p_elem_tail );
		p_domain->p_new_addrs->n = n;
	}else if( p_addrs != NULL ){
		addrs_ref_inc( p_addrs );
		p_domain->p_new_addrs = p_addrs;
	}
	if( p_domain->p_new_addrs != NULL ){
		gettimeofday( &p_domain->p_new_addrs->tv, NULL );
	}
	p_domain->p_addrs = p_domain->p_new_addrs;
	p_domain->p_new_addrs = NULL;
	return p_addrs_del;
}

static dns_addrs save_addrs( struct hostent * hostent )
{
	dns_addrs p_addrs;
	addr_elem p_elem;
	int i;

	p_addrs = new_addrs();
	if( p_addrs != NULL ){
		for( i = 0; hostent->h_addr_list[ i ] != NULL; i++ ){
			p_elem = dlist_malloc( sizeof( *p_elem ) );
			if( p_elem != NULL ){
				p_elem->family = hostent->h_addrtype;
				p_elem->len = MIN( hostent->h_length, sizeof( p_elem->addr_buf ) );
				memcpy( p_elem->addr_buf, hostent->h_addr_list[ i ], p_elem->len );
				dlist_insert( p_addrs->p_tail, p_elem );
			}else{
				MOON_PRINT_MAN( ERROR, "malloc addr_elem_s error!" );
				break;
			}
		}
		p_addrs->n = i;
		if( p_addrs->n == 0 ){
			addrs_ref_dec( p_addrs );
			p_addrs = NULL;
		}
	}
	return p_addrs;
}

void domain_callback( void * p_data, int status, int timeouts, struct hostent * hostent )
{
	dns_domain p_domain, p_domain_del;
	dns_private p_dns;
	dns_addrs p_addrs, p_addrs_del, p_addrs_query;
	dns_query p_query;

	p_query = NULL;
	p_domain = p_data;
	p_dns = p_domain->p_dns;
	p_addrs_query = p_addrs = p_addrs_del =  NULL;
	p_domain_del = NULL;
	if( status == ARES_SUCCESS ){
		MOON_PRINT( TEST, NULL, "domain_callback ok" );
		p_addrs = save_addrs( hostent );	
	}else{
		MOON_PRINT_MAN( ERROR, "query fail:%d", status );
	}
	pthread_mutex_lock( &p_dns->mutex );
	switch( p_domain->status ){
	case DOMAIN_QUERYING:
	case DOMAIN_NEW_QUERYING:
		addrs_ref_inc( p_addrs );
		p_domain->p_new_addrs = p_addrs;
		p_domain->status++;
		break;
	case DOMAIN_NEW_QUERY1:
		dlist_del( p_domain );
		p_dns->domain_num--;
	case DOMAIN_QUERY1:
		p_domain->status = DOMAIN_QUERY2;
		p_addrs_del = merge_addrs( p_domain, p_addrs );
		p_query = p_domain->p_query_head;
		for( ;p_query != NULL; p_query = dlist_next( p_query ) ){
			p_query->status = 1;
		}
		p_query = p_domain->p_query_head;
		p_domain->p_query_head = NULL;
		if( p_domain->p_addrs != NULL ){
			dlist_insert( p_dns->p_domain_tail, p_domain );
			p_dns->domain_num++;
			if( p_dns->domain_num > DNS_CACHE_NUM ){
				p_domain_del = dlist_next( p_dns->p_domain_head );
				dlist_del( p_domain_del );
				p_dns->domain_num--;
				if( p_domain_del->status == DOMAIN_QUERY2 ){
					hash_del( data_to_list( p_domain ) );
				}else{
					p_domain->status -= DOMAIN_QUERY2;
					p_domain_del = NULL;
				}
			}
			if( p_query != NULL ){
				p_addrs_query = p_domain->p_addrs;
				addrs_ref_inc( p_domain->p_addrs );
			}
		}else{
			hash_del( data_to_list( p_domain ) );
			p_domain_del = p_domain;
		}
		break;
	default:
		MOON_PRINT_MAN( ERROR, "bad domain callback status:%d", p_domain->status );
	}
	pthread_mutex_unlock( &p_dns->mutex );
	notice_user( p_query, p_addrs_query );
	if( p_domain_del != NULL ){
		addrs_ref_dec( p_domain_del->p_addrs );
		hash_free( data_to_list( p_domain_del ) );
	}
	addrs_ref_dec( p_addrs_del );
	addrs_ref_dec( p_addrs );
}

static void sock_state_change( void * p_data, ares_socket_t fd, int readable, int writeable )
{
	dns_private p_dns;
	struct epoll_event ev;
	int ret;

	MOON_PRINT( TEST, NULL, "state_change:%d:%d", readable, writeable );
	p_dns = p_data;
	ev.data.fd = fd;
	ev.events = ( readable * EPOLLIN ) | ( writeable * EPOLLOUT );
	if( ev.events != 0 ){
		ret = epoll_ctl( p_dns->epoll_fd, EPOLL_CTL_MOD, fd, &ev );
		if( ret < 0 ){
			ret = epoll_ctl( p_dns->epoll_fd, EPOLL_CTL_ADD, fd, &ev );
			if( ret < 0 ){
				MOON_PRINT_MAN( ERROR, "fatal error can't add to epoll!" );
			}
		}
	}else{//close
		epoll_ctl( p_dns->epoll_fd, EPOLL_CTL_DEL, fd, &ev );
	}
}

//所有c-ares的代码都在这个线程中运行
void * dns_task( void * arg )
{
	char * key;
	list p_list;
	uint64_t u64;
	struct timeval tv;
	struct ares_options opts;
	dns_private p_dns;
	int ret, nfds, i, fd, read_fd, write_fd, optmask;
	int processed;
	struct epoll_event events[ 5 ];
	dns_domain p_domain, p_domain_del, p_domain_tmp;

	p_dns = arg;
	optmask = 0;
	optmask |= ARES_OPT_TIMEOUT;
	opts.timeout = 10;
//	optmask |= ARES_OPT_TRIES;
//	opts.tries = 1;
	optmask |= ARES_OPT_SOCK_STATE_CB;
	opts.sock_state_cb = sock_state_change;
	opts.sock_state_cb_data = p_dns;
	if( ares_init_options( &p_dns->p_ares, &opts, optmask ) != ARES_SUCCESS ){
		MOON_PRINT_MAN( ERROR, "ares init error!" );
		return NULL;
	}
	while( 1 ){
		nfds = epoll_wait( p_dns->epoll_fd, events, 5, 1000 );
		if( nfds < 0 ){
			MOON_PRINT_MAN( ERROR, "epoll wait error!" );
			usleep( 100000 );
			continue;
		}
		processed = 0;
		for( i = 0; i < nfds; i++ ){
			fd = events[ i ].data.fd;
			if( fd == p_dns->event_fd ){
				ret = read( fd, &u64, sizeof( u64 ) );
				if( ret < 0 ){
					MOON_PRINT_MAN( ERROR, "eventfd error!" );
				}
				continue;
			}
			write_fd = read_fd = ARES_SOCKET_BAD;
			if( events[ i ].events & EPOLLOUT ){
				write_fd = fd;
			}
			if( events[ i ].events & ( EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP ) ){
				read_fd = fd;
			}
			processed = 1;
			ares_process_fd( p_dns->p_ares, read_fd, write_fd );
		}
		if( processed == 0 ){//let cares process timeout
			ares_process_fd( p_dns->p_ares, ARES_SOCKET_BAD, ARES_SOCKET_BAD );
		}
		//add new
		do{
			p_list = p_dns->p_add_cares;
		}while( !__sync_bool_compare_and_swap( &p_dns->p_add_cares, p_list, NULL ) );
		for( ; p_list != NULL; p_list = p_list->next ){
			p_domain = ( dns_domain )p_list - 1;
			key = ( ( ( hash_key )data_to_list( p_domain ) ) - 1 )->key;
			ares_gethostbyname( p_dns->p_ares, key, AF_INET6, domain_callback, p_domain );
			ares_gethostbyname( p_dns->p_ares, key, AF_INET, domain_callback, p_domain );
		}
		//del old
		gettimeofday( &tv, NULL );
		tv.tv_sec -= DNS_TIMEOUT_S;
		pthread_mutex_lock( &p_dns->mutex );
		p_domain = dlist_next( p_dns->p_domain_head );
		p_domain_del = NULL;
		while( p_domain != p_dns->p_domain_tail ){
			if( compare_tv( &p_domain->p_addrs->tv, &tv ) <= 0 ){
				p_domain_tmp = p_domain;
				p_domain = dlist_del( p_domain_tmp );
				p_dns->domain_num--;
				if( p_domain->status == DOMAIN_QUERY2 ){
					hash_del( data_to_list( p_domain ) );
					p_domain_del = dlist_insert( p_domain_del, p_domain_tmp );
				}else{
					p_domain->status -= DOMAIN_QUERY2;
				}
			}else{
				break;
			}
		}
		pthread_mutex_unlock( &p_dns->mutex );
		while( p_domain_del != NULL ){
			p_domain_tmp = p_domain_del;
			p_domain_del = dlist_del( p_domain_tmp );
			addrs_ref_dec( p_domain_tmp->p_addrs );
			hash_free( data_to_list( p_domain_tmp ) );
		}
	}
	return NULL;
}

//addrs
static dns_addrs new_addrs( )
{
	dns_addrs p_addrs;

	p_addrs = MALLOC_INTERFACE_ENTITY( sizeof( dns_addrs_s ), 0, 0 );
	if( p_addrs != NULL ){
		BEGAIN_INTERFACE( p_addrs );
		END_INTERFACE( GET_STATIC_INTERFACE_HANDLE( dns_addrs_hub ) );
		p_addrs->ref_num = 1;
		p_addrs->p_head = list_to_data( &p_addrs->dlist[ 0 ] );
		p_addrs->p_tail = list_to_data( &p_addrs->dlist[ 1 ] );
		dlist_append( p_addrs->p_head, p_addrs->p_tail );
	}
	return p_addrs;
}

static int get_addrs_num( void * p_data )
{
	if( p_data != NULL ){
		return ( ( dns_addrs )p_data )->n;
	}
	return 0;
}

static int get_addrs_timestap( void * p_data, struct timeval * p_tv )
{
	if( p_data != NULL && p_tv != NULL ){
		*p_tv = ( ( dns_addrs )p_data )->tv;
		return 0;
	}
	return -1;
}

static addr_elem get_addrs_next( void * p_data, addr_elem p_elem )
{
	dns_addrs p_addrs;
	addr_elem p_next_elem;

	if( p_data != NULL ){
		p_addrs = p_data;
		if( p_elem != NULL ){
			p_next_elem = dlist_next( p_elem );
		}else{
			p_next_elem = dlist_next( p_addrs->p_head );
		}
		return p_next_elem != p_addrs->p_tail ? p_next_elem : NULL;
	}
	return NULL;
}

static addr_elem get_addrs_prev( void * p_data, addr_elem p_elem )
{
	dns_addrs p_addrs;
	addr_elem p_prev_elem;

	if( p_data != NULL ){
		p_addrs = p_data;
		if( p_elem != NULL ){
			p_prev_elem = dlist_prev( p_elem );
		}else{
			p_prev_elem = dlist_prev( p_addrs->p_tail );
		}
		return p_prev_elem != p_addrs->p_head ? p_prev_elem : NULL;
	}
	return NULL;
}

static void addrs_ref_inc( void * p_data )
{
	if( p_data != NULL ){
		GC_REF_INC( ( dns_addrs )p_data );
	}
}

static void addrs_ref_dec( void * p_data )
{
	int ref_num;
	dns_addrs p_addrs;
	addr_elem p_elem, p_elem_tmp;

	if( p_data != NULL ){
		p_addrs = p_data;
		ref_num = GC_REF_DEC( p_addrs );
		if( ref_num == 0 ){
			p_elem = dlist_next( p_addrs->p_head );
			while( p_elem != p_addrs->p_tail ){
				p_elem_tmp = p_elem;
				p_elem = dlist_del( p_elem_tmp );
				dlist_free( p_elem_tmp );
			}
			free( GET_INTERFACE_START_POINT( p_addrs ) );	
		}
	}
}

