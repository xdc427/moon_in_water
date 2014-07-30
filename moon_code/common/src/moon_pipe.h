#ifndef _MOON_PIPE_H_
#define _MOON_PIPE_H_
#include"moon_interface.h"

typedef struct pipe_interface_s{
	void ( *init_done )( void *, int );
	int ( * set_point_ref )( void *, void * );
	int ( * get_other_point_ref )( void *, void **, void ** );
	void ( * close )( void * );
} pipe_interface_s;
typedef pipe_interface_s * pipe_interface;
DECLARE_INTERFACE( pipe_interface_s );

typedef struct pipe_listener_interface_s{
	void ( * close )( void *, void * );
	void ( * get_pipe_data_len )( void *, int * );
	void ( * free_pipe_data )( void * );
} pipe_listener_interface_s;
typedef pipe_listener_interface_s * pipe_listener_interface;
DECLARE_INTERFACE( pipe_listener_interface_s );

int pipe_new( void ** ptr, int len1, int len2, int is_two_way );

#endif

