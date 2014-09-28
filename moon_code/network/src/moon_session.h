#ifndef _MOON_SESSION_H_
#define _MOON_SESSION_H_
#include"moon_interface.h"

int session_new( void * p_pipe, int is_server );
typedef struct session_interface_s{
	int link_io( void * p_data, void * p_pipe, void * p_io_pipe );
	int new_stream( void * p_data, void * p_pipe, void * p_new_pipe );
} session_interface_s;
typedef session_interface_s * session_interface;
DECLARE_INTERFACE( session_interface_s );

enum{
	STREAM_EVENT_TIMEOUT = -1,
	STREAM_EVENT_CLOSED = -2
};

//stream control
enum{
	STREAM_NEW,
	STREAM_NEW_PUSHED,
	STREAM_IS_PUSHED,
	STREAM_GET_MAIN,
	STREAM_GET_PUSHED,
	STREAM_SET_WINDOW_SIZE,
	STREAM_SET_SENDBUF_SIZE,
	STREAM_SET_CAN_READ,
	STREAM_SET_CAN_WRITE
};

#endif

