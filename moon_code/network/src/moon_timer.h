#ifndef _MOON_TIMER_H_
#define _MOON_TIMER_H_
#include"moon_common.h"
#include"moon_interface.h"

typedef struct{
	int mark_id;
	int repeat_num;// < 0 无限次
	unsigned long long time_us;
} timer_desc_s;
typedef timer_desc_s * timer_desc;

void * get_timer_instance();

typedef struct{
	int ( *new_timer )( void * p_timer, void * p_pipe, timer_desc p_desc );
	int ( *set )( void * p_timer, void * p_pipe, timer_desc p_desc );
	int ( *del )( void * p_timer, void * p_pipe );
	int ( *stop )( void * p_timer, void * p_pipe );
	int ( *restart )( void * p_timer, void * p_pipe );
} timer_interface_s;
DECLARE_INTERFACE( timer_interface_s );

typedef struct{
	void ( *times_up )( void * p_data, void * p_pipe, int mark_id );
} timer_listen_interface_s;
DECLARE_INTERFACE( timer_listen_interface_s );

#endif
