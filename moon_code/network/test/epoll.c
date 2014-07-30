#include<stdio.h>
#include<string.h>
#include<unistd.h>
#include<pthread.h>
#include<stdlib.h>
#include<time.h>
#include<fcntl.h>
#include<errno.h>
#include<netdb.h>
#include<sys/socket.h>
#include<sys/epoll.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/types.h>
#include<signal.h>
#include"moon_debug.h"

int accept_fd;
int listen_fd;
int epoll_fd;
int client_fd;

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

static void * listen_task( void * arg )
{
	int i, nfds, ret;
	int fd, result;
	socklen_t result_len;
	struct epoll_event events[ 30 ], ev;
	socklen_t addr_len;
	struct sockaddr_storage addr;

	for( ; ; ){
		nfds = epoll_wait( epoll_fd, events, 30, 100 );
		if( nfds < 0 ){
			MOON_PRINT_MAN( ERROR, "epoll wait error!" );
			usleep( 100000 );
			continue;
		}
		if( nfds > 0 ){
			MOON_PRINT( TEST, NULL, "----------" );
		}
		for( i = 0; i < nfds; i++ ){
			fd = events[ i ].data.fd;
			if( events[ i ].events & EPOLLIN ){
				MOON_PRINT( TEST, NULL, "%d:EPOLLIN", fd );
				if( fd == listen_fd ){
					addr_len = sizeof( addr );
					ret = accept( listen_fd, ( struct sockaddr * )&addr, &addr_len );
					ev.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
					ev.data.fd = ret;
					accept_fd = ret;
					set_socket_nonblock( accept_fd );
					MOON_PRINT( TEST, NULL, "accept_scoket:%d", accept_fd );
					ret = epoll_ctl( epoll_fd, EPOLL_CTL_ADD, accept_fd, &ev );
				}
			}
			if( events[ i ].events & EPOLLOUT ){
				MOON_PRINT( TEST, NULL, "%d:EPOLLOUT", fd );				
				if( fd == client_fd ){
					result_len = sizeof( result );
					ret = getsockopt( fd, SOL_SOCKET, SO_ERROR, &result, &result_len );
					if( ret < 0 || result != 0 ){
						MOON_PRINT( TEST, NULL, "connect error:%d:%d", fd, result );
					}
				}
			}
			if( events[ i ].events & EPOLLRDHUP ){
				MOON_PRINT( TEST, NULL, "%d:EPOLLRDHUP", fd );
			}
			if( events[ i ].events & EPOLLERR ){
				MOON_PRINT( TEST, NULL, "%d:EPOLLERR", fd );
			}
			if(	events[ i ].events & EPOLLHUP ){//close
				MOON_PRINT( TEST, NULL, "%d:EPOLLHUP", fd );
			}
		}
	}
	return NULL;
}

void main()
{
	struct addrinfo hints, * p_results;
	int new_fd, ret, reuse;
	struct epoll_event ev;
	pthread_t pt;

	signal( SIGPIPE, SIG_IGN );
	epoll_fd = epoll_create( 1000 );
	if( __builtin_expect( epoll_fd < 0, 0 ) ){
		MOON_PRINT_MAN( ERROR, "create epoll error!" );
		goto back;
	}
	ret = pthread_create( &pt, NULL, listen_task, NULL );

	memset( &hints, 0, sizeof( hints ) );
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if( __builtin_expect( getaddrinfo( NULL, "12345", &hints, &p_results ) != 0, 0 ) ){
		MOON_PRINT_MAN( ERROR, "get addr info error!" );
		goto back;
	}
	new_fd = socket( p_results->ai_family, p_results->ai_socktype, p_results->ai_protocol );
	if( __builtin_expect( new_fd < 0, 0 ) ){
		MOON_PRINT_MAN( ERROR, "new sockert error" );
		goto back;
	}
	MOON_PRINT( TEST, NULL, "listen_socket:%d", new_fd );
	if( setsockopt( new_fd, SOL_SOCKET, SO_REUSEADDR, ( char * )&reuse, sizeof( reuse ) ) < 0
		|| set_socket_nonblock( new_fd ) < 0 
		|| bind( new_fd, p_results->ai_addr, p_results->ai_addrlen ) < 0 
		|| listen( new_fd , 15 ) < 0 ){
		MOON_PRINT_MAN( ERROR, "bind or listen error!" );
		goto back;
	}
	ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
	ev.data.fd = new_fd;
	listen_fd = new_fd;
	epoll_ctl( epoll_fd, EPOLL_CTL_ADD, new_fd, &ev );
	
	memset( &hints, 0, sizeof( hints ) );
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICHOST;
	if( __builtin_expect( getaddrinfo( "127.0.0.1", "12345", &hints, &p_results ) != 0, 0 ) ){
		MOON_PRINT_MAN( ERROR, "get addr info error!" );
		goto back;
	}
	new_fd = socket( p_results->ai_family, p_results->ai_socktype, p_results->ai_protocol );
	if( __builtin_expect( new_fd < 0, 0 ) ){
		MOON_PRINT_MAN( ERROR, "new sockert error" );
		goto back;
	}
	if( __builtin_expect( set_socket_nonblock( new_fd ) < 0, 0 ) ){
		MOON_PRINT_MAN( ERROR, "set socket nio error!" );
		goto back;
	}
	ret = connect( new_fd, p_results->ai_addr, p_results->ai_addrlen );
	ev.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
	ev.data.fd = new_fd;
	client_fd = new_fd;
	MOON_PRINT( TEST, NULL, "client_socket:%d", new_fd );
	epoll_ctl( epoll_fd, EPOLL_CTL_ADD, new_fd, &ev );

	char buf[ 1024 * 1024  ];
	sleep( 1 );
//	send( client_fd, buf, sizeof( buf ), 0 );
//	sleep( 1 );
//	send( client_fd, buf, sizeof( buf ), 0 );
	close( client_fd );
	sleep( 1 );

	ret = recv( accept_fd, buf, sizeof( buf ), 0 );
	MOON_PRINT( TEST, NULL, "recv:%d", ret );
	ret = send( accept_fd, buf, sizeof( buf ), 0 );
	MOON_PRINT( TEST, NULL, "send:%d", ret );
	sleep( 1 );
	ret = recv( accept_fd, buf, sizeof( buf ), 0 );
	MOON_PRINT( TEST, NULL, "recv:%d", ret );
	ret = send( accept_fd, buf, sizeof( buf ), 0 );
	MOON_PRINT( TEST, NULL, "send:%d", ret );
	sleep( 1 );
	ret = recv( accept_fd, buf, sizeof( buf ), 0 );
	MOON_PRINT( TEST, NULL, "recv:%d", ret );
	ret = send( accept_fd, buf, sizeof( buf ), 0 );
	MOON_PRINT( TEST, NULL, "send:%d", ret );

while(1);
back:
	return;
}


