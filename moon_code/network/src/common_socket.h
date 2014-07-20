#ifndef _COMMON_SOCKET_H_
#define _COMMON_SOCKET_H_
#include"moon_interface.h"

enum{
	SOCKET_ERROR = -1,
	SOCKET_CLOSED = -2,
	SOCKET_NO_RES = -3,
	SOCKET_PARAERROR = -4
};

enum{
	SOCKET_START
};

//instatce实现socketpool_interface_s 和 pipe_interface_s 接口
void * get_socketpool_instance();

typedef struct socketpool_interface_s{
	//p_pipe 的另一端 实现order_listener_interface_s 和 pipe_interface_s
	int ( *new_listen_socket )( void * p_pool, void * p_pipe, const char * port );
	int ( *new_socket )( void * p_pool, void * p_pipe, const char * ip, const char * port );
} socketpool_interface_s;
typedef socketpool_interface_s * socketpool_interface;
DECLARE_INTERFACE( socketpool_interface_s );

#endif

