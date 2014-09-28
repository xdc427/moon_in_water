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
int calculate_pipe_len( int len1, int len2 );
int pipe_new2( void ** ptr, int len1, int len2, int is_two_way, void * p_buf );

#define CALL_PIPE_POINT_FUNC( p_pipe, i_type, i_func, ... ) ({\
	void * _p_point, * _p_point_data;\
	pipe_interface _p_pipe_i;\
	\
	_p_pipe_i = FIND_INTERFACE( p_pipe, pipe_interface_s );\
	if( _p_pipe_i->get_other_point_ref( p_pipe, &_p_point, &_p_point_data ) >= 0 ){\
		CALL_INTERFACE_FUNC( _p_point, i_type, i_func, _p_point_data, ##__VA_ARGS__ );\
		CALL_INTERFACE_FUNC( _p_point, gc_interface_s, ref_dec );\
		CALL_INTERFACE_FUNC( _p_point_data, gc_interface_s, ref_dec );\
	}\
	_p_pipe_i;\
})

#define CALL_PIPE_POINT_FUNC_RET( ret, p_pipe, i_type, i_func, ... ) ({\
	void * _p_point, * _p_point_data;\
	pipe_interface _p_pipe_i;\
	\
	_p_pipe_i = FIND_INTERFACE( p_pipe, pipe_interface_s );\
	if( _p_pipe_i->get_other_point_ref( p_pipe, &_p_point, &_p_point_data ) >= 0 ){\
		ret = CALL_INTERFACE_FUNC( _p_point, i_type, i_func, _p_point_data, ##__VA_ARGS__ );\
		CALL_INTERFACE_FUNC( _p_point, gc_interface_s, ref_dec );\
		CALL_INTERFACE_FUNC( _p_point_data, gc_interface_s, ref_dec );\
	}\
	_p_pipe_i;\
})

#endif

