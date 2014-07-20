#include<sys/time.h>
#include<stdio.h>
#include<stdlib.h>
#include<pthread.h>
#include"moon_pipe.h"
#include"moon_timer.h"
#include"common_interfaces.h"

#ifdef MODENAME
#undef MODENAME
#endif
#define MODENAME "moon_timer"

enum{
	TIMER_PROCESSING = 0x1,
	TIMER_STOP = 0x2,
	TIMER_DEL = 0x4 
};
//double_list_s + timer_inter_s
typedef struct{
	int status;
	int prev_mark_id;
	addr_pair p_pair;
	timer_desc_s desc;
} timer_inter_s;
typedef timer_inter_s * timer_inter;

typedef struct{
	int status;
	avl_tree p_timer_avl;
	addr_pair p_earliest_pair;
	pthread_mutex_t timer_mutex;
	pthread_cond_t timer_cond;
	struct timeval tv_base;
	pthread_t timer_pthread;
} timer_runtime_s;
typedef timer_runtime_s * timer_runtime;

static void * timer_task( void * p_arg );
static int timer_new( void * p_data, void * p_pipe, timer_desc p_desc );
static void get_pipe_data_len( void * p_data,  int * p_len );
static int timer_del( void * p_data, void * p_pipe );
static int timer_set( void * p_data, void * p_pipe, timer_desc p_desc );
static int timer_stop( void * p_data, void * p_pipe );
static int timer_restart( void * p_data, void * p_pipe );

STATIC_BEGAIN_INTERFACE( timer_hub )
STATIC_DECLARE_INTERFACE( timer_interface_s )
STATIC_DECLARE_INTERFACE( pipe_listener_interface_s )
STATIC_END_DECLARE_INTERFACE( timer_hub, 2, timer_runtime_s timer )
STATIC_GET_INTERFACE( timer_hub, timer_interface_s, 0 ) = {
	.new_timer = timer_new,
	.del = timer_del,
	.stop = timer_stop,
	.restart = timer_restart,
	.set = timer_set
}
STATIC_GET_INTERFACE( timer_hub, pipe_listener_interface_s, 1 ) = {
	.close = NULL,
	.get_pipe_data_len = get_pipe_data_len
}
STATIC_INIT_USERDATA( timer ) = {
	.status = 0,
	.p_timer_avl = NULL,
	.p_earliest_pair = NULL,
	.timer_mutex = PTHREAD_MUTEX_INITIALIZER,
	.timer_cond = PTHREAD_COND_INITIALIZER
}
STATIC_END_INTERFACE( NULL )

void * get_timer_instance()
{
	timer_runtime p_timer;
	pthread_attr_t attr;

	p_timer = &timer_hub.timer;
	if( __builtin_expect( p_timer->status == 0, 0 ) ){
		pthread_mutex_lock( &p_timer->timer_mutex );
		if( p_timer->status == 0 ){
			gettimeofday( &p_timer->tv_base, NULL );
			pthread_attr_init( &attr );
			pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
			pthread_create( &p_timer->timer_pthread, &attr, timer_task, p_timer );
			pthread_attr_destroy( &attr );
			p_timer->status = 1;
		}
		pthread_mutex_unlock( &p_timer->timer_mutex );
	}
	return p_timer;
}

static void get_pipe_data_len( void * p_data, int * p_len )
{
	*p_len = sizeof( double_list_s ) + sizeof( timer_inter_s );
}


static inline unsigned long long timeval_offset_us( struct timeval * p_cur_tv, struct timeval * p_base_tv )
{
	return ( unsigned long long )( p_cur_tv->tv_sec - p_base_tv->tv_sec ) * 1000000
		+ p_cur_tv->tv_usec - p_base_tv->tv_usec;
}

static inline void convert_timespec( struct timespec * p_ts, struct timeval * p_tv, unsigned long long us )
{
	p_tv->tv_sec += us / 1000000;
	p_tv->tv_usec += us % 1000000;
	p_ts->tv_sec = p_tv->tv_sec + ( p_tv->tv_usec / 1000000 );
	p_ts->tv_nsec = ( p_tv->tv_usec % 1000000 ) * 1000;
}

static void * timer_task( void * p_arg )
{
	timer_runtime p_timer;
	struct timespec ts;
	struct timeval tv;
	unsigned long long id, curtime_us;
	timer_inter p_inter, p_inter_tmp, p_inter_dels;
	void * p_point, * p_point_data;
	timer_listen_interface_s * p_listen_i;
	pipe_interface_s * p_pipe_i;
	addr_pair p_pair;
#ifdef MOON_TEST
	int i;
#endif

	p_timer = ( timer_runtime )p_arg;
	pthread_mutex_lock( &p_timer->timer_mutex );
	for( ; ; ){
		gettimeofday( &tv, NULL );
		if( p_timer->p_earliest_pair == NULL ){
			convert_timespec( &ts, &tv, 1000000 );
			pthread_cond_timedwait( &p_timer->timer_cond, &p_timer->timer_mutex, &ts );
			continue;
		}
		curtime_us = timeval_offset_us( &tv, &p_timer->tv_base ); 
		p_inter = ( timer_inter )p_timer->p_earliest_pair->ptr;
		id = p_timer->p_earliest_pair->id;
		if( id > curtime_us ){//此处可以设置精度
			convert_timespec( &ts, &tv, MIN( id - curtime_us, 1000000 ) );
			pthread_cond_timedwait( &p_timer->timer_cond, &p_timer->timer_mutex, &ts );
			continue;
		}
		avl_del( &p_timer->p_timer_avl, id );
#ifdef MOON_TEST
		i = 0;
#endif
		for( p_inter_tmp = p_inter
			; p_inter_tmp != NULL; p_inter_tmp = dlist_next( p_inter_tmp ) ){
			p_inter_tmp->p_pair = NULL;
			p_inter_tmp->prev_mark_id = p_inter_tmp->desc.mark_id;
			p_inter_tmp->status |= TIMER_PROCESSING;
			if( p_inter_tmp->desc.repeat_num >= 0 ){
				p_inter_tmp->desc.repeat_num--;
			}
#ifdef MOON_TEST
			i++;
#endif
		}
		MOON_PRINT( TEST, NULL, "timesup_num:%d", i );
		p_timer->p_earliest_pair = avl_leftest_node( p_timer->p_timer_avl );
		pthread_mutex_unlock( &p_timer->timer_mutex );
		for( p_inter_tmp = p_inter
			; p_inter_tmp != NULL; p_inter_tmp = dlist_next( p_inter_tmp ) ){
			p_point_data = p_point = NULL;
			p_pipe_i = FIND_INTERFACE( data_to_list( p_inter_tmp ), pipe_interface_s );
			if( p_pipe_i->get_other_point_ref( data_to_list( p_inter_tmp )
					, &p_point, &p_point_data ) >= 0 ){
				p_listen_i = FIND_INTERFACE( p_point, timer_listen_interface_s );
				p_listen_i->times_up( p_point, p_point_data, p_inter_tmp->prev_mark_id );
				CALL_INTERFACE_FUNC( p_point, gc_interface_s, ref_dec );
				CALL_INTERFACE_FUNC( p_point_data, gc_interface_s, ref_dec );
			}else{
				MOON_PRINT_MAN( ERROR, "can't find other point!" );
				p_inter_tmp->status |= TIMER_DEL;//此处不用锁
			}
		}
		pthread_mutex_lock( &p_timer->timer_mutex );
		gettimeofday( &tv, NULL );
		p_inter_dels = NULL;
		while( p_inter != NULL ){
			p_inter_tmp = p_inter;
			p_inter = dlist_next( p_inter );
			dlist_del( p_inter_tmp );
			p_inter_tmp->status &= ~TIMER_PROCESSING;
			if( ( p_inter_tmp->status & ( TIMER_DEL | TIMER_STOP ) ) == 0
				&& p_inter_tmp->desc.repeat_num != 0 ){
				id = timeval_offset_us( &tv, &p_timer->tv_base );
				id += p_inter_tmp->desc.time_us;
				p_pair = avl_add( &p_timer->p_timer_avl, id );
				if( p_pair != NULL ){
					p_pair->ptr = dlist_insert( p_pair->ptr, p_inter_tmp );
					p_inter_tmp->p_pair = p_pair;
				}else{//error
					p_inter_tmp->status |= TIMER_DEL;
					p_inter_dels = dlist_insert( p_inter_dels, p_inter_tmp );
				}
			}else if( ( p_inter_tmp->status & TIMER_DEL ) != 0 ){
				p_inter_dels = dlist_insert( p_inter_dels, p_inter_tmp );
			}
		}
		p_timer->p_earliest_pair = avl_leftest_node( p_timer->p_timer_avl );
		pthread_mutex_unlock( &p_timer->timer_mutex );
		while( p_inter_dels != NULL ){
			p_inter_tmp = p_inter_dels;
			p_inter_dels = dlist_next( p_inter_dels );
			dlist_del( p_inter_tmp );
			CALL_INTERFACE_FUNC( data_to_list( p_inter_tmp ), pipe_interface_s, close );
			CALL_INTERFACE_FUNC( data_to_list( p_inter_tmp ), gc_interface_s, ref_dec );
		}
		pthread_mutex_lock( &p_timer->timer_mutex );
	}
	return NULL;
}

static inline void timer_leave_avl( timer_runtime p_timer, timer_inter p_inter )
{
	if( p_inter->p_pair != NULL ){
		if( p_inter->p_pair->ptr == p_inter 
			&& dlist_next( p_inter ) == NULL ){
			avl_del( &p_timer->p_timer_avl, p_inter->p_pair->id );
			if( p_inter->p_pair == p_timer->p_earliest_pair ){
				p_timer->p_earliest_pair = avl_leftest_node( p_timer->p_timer_avl );
			}
		}else if( p_inter->p_pair->ptr == p_inter ){
			p_inter->p_pair->ptr = dlist_next( p_inter );
		}
		dlist_del( p_inter );
		p_inter->p_pair = NULL;
	}
}

static inline int timer_join_avl( timer_runtime p_timer, timer_inter p_inter )
{
	unsigned long long id;
	struct timeval tv;
	addr_pair p_pair;


	gettimeofday( &tv, NULL );
	id = timeval_offset_us( &tv, &p_timer->tv_base );
	id += p_inter->desc.time_us;
#ifdef MOON_TEST
	id /= 100000;
	id *= 100000;
#endif
	p_pair = avl_add( &p_timer->p_timer_avl, id );
	if( p_pair != NULL ){
		p_pair->ptr = dlist_insert( p_pair->ptr, p_inter );
		p_inter->p_pair = p_pair;
		if( p_timer->p_earliest_pair == NULL 
		|| p_pair->id < p_timer->p_earliest_pair->id ){
			pthread_cond_signal( &p_timer->timer_cond );
			p_timer->p_earliest_pair = p_pair;
		}
		return 0;
	}else{
		p_inter->status |= TIMER_DEL;
		return -1;
	}
}

static int timer_new( void * p_data, void * p_pipe, timer_desc p_desc )
{
	int ret;
	timer_runtime p_timer;
	timer_inter p_inter;
	pipe_interface_s * p_pipe_i;

	if( p_data == NULL || p_pipe == NULL || p_desc == NULL || p_desc->time_us == 0 ){
		MOON_PRINT_MAN( ERROR, "input parameters error!" );
		return -1;
	}
	ret = -1;
	p_timer = ( timer_runtime )p_data;
	p_inter = ( timer_inter )list_to_data( p_pipe );
	p_pipe_i = FIND_INTERFACE( p_pipe, pipe_interface_s );
	p_inter->desc = *p_desc;
	if( p_inter->desc.repeat_num == 0 ){
		p_inter->desc.repeat_num--;
	}
	pthread_mutex_lock( &p_timer->timer_mutex );
	if( timer_join_avl( p_timer, p_inter ) >= 0 ){
		CALL_INTERFACE_FUNC( p_pipe, gc_interface_s, ref_inc );
		ret = 0;
	}
	pthread_mutex_unlock( &p_timer->timer_mutex );
	if( ret == 0 ){
		p_pipe_i->set_point_ref( p_pipe, p_timer );
	}
	p_pipe_i->init_done( p_pipe, ret );
	return ret;
}

static int timer_del( void * p_data, void * p_pipe )
{
	timer_runtime p_timer;
	timer_inter p_inter;
	int can_del;

	if( p_data == NULL || p_pipe == NULL ){
		return -1;
	}
	can_del = 0;
	p_timer = ( timer_runtime )p_data;
	p_inter = ( timer_inter )list_to_data( p_pipe );
	pthread_mutex_lock( &p_timer->timer_mutex );
	if( ( p_inter->status & ( TIMER_DEL | TIMER_PROCESSING ) ) == 0 ){
		timer_leave_avl( p_timer, p_inter );
		can_del = 1;
	}
	p_inter->status |= TIMER_DEL;
	pthread_mutex_unlock( &p_timer->timer_mutex );
	if( can_del != 0 ){
		CALL_INTERFACE_FUNC( p_pipe, pipe_interface_s, close );
		CALL_INTERFACE_FUNC( p_pipe, gc_interface_s, ref_dec );
	}
	return 0;
}

//set 失败后处于del状态
static int timer_set( void * p_data, void * p_pipe, timer_desc p_desc )
{
	timer_runtime p_timer;
	timer_inter p_inter;
	int ret, can_del;

	if( p_data == NULL || p_pipe == NULL || p_desc == NULL || p_desc->time_us == 0 ){
		return -1;
	}
	ret = -1;
	can_del = 0;
	p_timer = ( timer_runtime )p_data;
	p_inter = ( timer_inter )list_to_data( p_pipe );
	pthread_mutex_lock( &p_timer->timer_mutex );
	if( ( p_inter->status & TIMER_DEL ) == 0 ){
		p_inter->desc = *p_desc;
		if( p_inter->desc.repeat_num == 0 ){
			p_inter->desc.repeat_num--;
		}
		p_inter->status &= ~TIMER_STOP;
		if( ( p_inter->status & TIMER_PROCESSING ) == 0 ){
			timer_leave_avl( p_timer, p_inter );
			ret = timer_join_avl( p_timer, p_inter );
			can_del = ret;
		}else{
			ret = 0;
		}
	}
	pthread_mutex_unlock( &p_timer->timer_mutex );
	if( can_del != 0 ){
		CALL_INTERFACE_FUNC( p_pipe, pipe_interface_s, close );
		CALL_INTERFACE_FUNC( p_pipe, gc_interface_s, ref_dec );
	}
	return ret;
}

static int timer_stop( void * p_data, void * p_pipe )
{
	timer_runtime p_timer;
	timer_inter p_inter;

	if( p_data == NULL || p_pipe == NULL ){
		return -1;
	}
	p_timer = ( timer_runtime )p_data;
	p_inter = ( timer_inter )list_to_data( p_pipe );
	pthread_mutex_lock( &p_timer->timer_mutex );
	if( ( p_inter->status & ( TIMER_DEL | TIMER_PROCESSING ) ) == 0 ){
		timer_leave_avl( p_timer, p_inter );
	}
	p_inter->status |= TIMER_STOP;
	pthread_mutex_unlock( &p_timer->timer_mutex );
	return 0;
}

static int timer_restart( void * p_data, void * p_pipe )
{
	timer_runtime p_timer;
	timer_inter p_inter;
	int ret, can_del;

	if( p_data == NULL || p_pipe == NULL ){
		return -1;
	}
	ret = -1;
	can_del = 0;
	p_timer = ( timer_runtime )p_data;
	p_inter = ( timer_inter )list_to_data( p_pipe );
	pthread_mutex_lock( &p_timer->timer_mutex );
	if( ( p_inter->status & TIMER_DEL ) == 0 ){
		p_inter->status &= ~TIMER_STOP;
		if( ( p_inter->status & TIMER_PROCESSING ) == 0 
			&& p_inter->p_pair == NULL 
			&& p_inter->desc.repeat_num != 0 ){
			ret = timer_join_avl( p_timer, p_inter );
			can_del = ret;
		}else{
			ret = 0;
		}
	}
	pthread_mutex_unlock( &p_timer->timer_mutex );
	if( can_del != 0 ){
		CALL_INTERFACE_FUNC( p_pipe, pipe_interface_s, close );
		CALL_INTERFACE_FUNC( p_pipe, gc_interface_s, ref_dec );
	}
	return ret;
}

