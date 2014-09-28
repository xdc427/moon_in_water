#include<sys/time.h>
#include<stdio.h>
#include<stdlib.h>
#include<pthread.h>
#include"moon_pipe.h"
#include"moon_timer.h"
#include"common_interfaces.h"
#include"moon_thread_info.h"

#ifdef MODENAME
#undef MODENAME
#endif
#define MODENAME "moon_timer"

enum{
	TIMER_PROCESSING = 0x1,
	TIMER_STOP = 0x2,
	TIMER_DEL = 0x4 
};
//avl_tree_s + double_list_s + timer_inter_s
typedef struct{
	unsigned long long id;
	int status;
	int prev_mark_id;
	timer_desc_s desc;
} timer_inter_s;
typedef timer_inter_s * timer_inter;

typedef struct{
	int status;
	avl_tree p_timer_avl;
	timer_inter p_earliest_timer;
	pthread_mutex_t timer_mutex;
	pthread_cond_t timer_cond;
	struct timeval tv_base;
	pthread_t timer_pthread;
} timer_runtime_s;
typedef timer_runtime_s * timer_runtime;

static void * timer_task( void * p_arg );
static int timer_new( void * p_data, void * p_pipe, timer_desc p_desc );
static void get_pipe_data_len( void * p_data,  int * p_len );
static void timer_del( void * p_data, void * p_pipe );
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
	.close = timer_del,
	.get_pipe_data_len = get_pipe_data_len
}
STATIC_INIT_USERDATA( timer ) = {
	.status = 0,
	.p_timer_avl = NULL,
	.p_earliest_timer = NULL,
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
	*p_len = AVL_NODE_APPEND_LEN + DLIST_NODE_APPEND_LEN + sizeof( timer_inter_s );
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

static int timer_cmp_func( void * p0, common_user_data_u user_data )
{
	uint64_t u64_0, u64_1;

	u64_0 = ( ( timer_inter )list_to_data( p0 ) )->id;
	u64_1 = user_data.ull_num;
	if( u64_0 > u64_1 ){
		return 1;
	}else if( u64_0 < u64_1 ){
		return -1;
	}else{
		return 0;
	}
}

static void * timer_task( void * p_arg )
{
	timer_runtime p_timer;
	struct timespec ts;
	struct timeval tv;
	unsigned long long id, curtime_us;
	timer_inter p_inter, p_inter_tmp, p_inter_dels, p_inter_cmp;
	void * p_point, * p_point_data;
	timer_listener_interface_s * p_listen_i;
	pipe_interface_s * p_pipe_i;
	common_user_data_u user_data;
	avl_tree p_avl;
	thread_info p_info;
#ifdef MOON_TEST
	int i;
#endif

	if( ( p_info = init_thread() ) != NULL ){
		p_info->level = THREAD_LEVEL0;
	}
	p_timer = ( timer_runtime )p_arg;
	pthread_mutex_lock( &p_timer->timer_mutex );
	for( ; ; ){
		gettimeofday( &tv, NULL );
		if( p_timer->p_earliest_timer == NULL ){
			convert_timespec( &ts, &tv, 1000000 );
			pthread_cond_timedwait( &p_timer->timer_cond, &p_timer->timer_mutex, &ts );
			continue;
		}
		curtime_us = timeval_offset_us( &tv, &p_timer->tv_base ); 
		p_inter = p_timer->p_earliest_timer;
		if( p_inter->id > curtime_us ){//此处可以设置精度
			convert_timespec( &ts, &tv, MIN( p_inter->id - curtime_us, 1000000 ) );
			pthread_cond_timedwait( &p_timer->timer_cond, &p_timer->timer_mutex, &ts );
			continue;
		}
		avl_del2( &p_timer->p_timer_avl, data_to_list( p_inter ) );
#ifdef MOON_TEST
		i = 0;
#endif
		for( p_inter_tmp = p_inter
			; p_inter_tmp != NULL; p_inter_tmp = dlist_next( p_inter_tmp ) ){
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
		p_timer->p_earliest_timer = list_to_data( avl_leftest_node( p_timer->p_timer_avl ) );
		pthread_mutex_unlock( &p_timer->timer_mutex );
		for( p_inter_tmp = p_inter
			; p_inter_tmp != NULL; p_inter_tmp = dlist_next( p_inter_tmp ) ){
			p_point_data = p_point = NULL;
			p_avl = data_to_avl( data_to_list( p_inter_tmp ) );
			p_pipe_i = FIND_INTERFACE( p_avl, pipe_interface_s );
			if( p_pipe_i->get_other_point_ref( p_avl, &p_point, &p_point_data ) >= 0 ){
				p_listen_i = FIND_INTERFACE( p_point, timer_listener_interface_s );
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
			p_inter = dlist_del( p_inter_tmp );
			p_inter_tmp->status &= ~TIMER_PROCESSING;
			if( ( p_inter_tmp->status & ( TIMER_DEL | TIMER_STOP ) ) == 0
				&& p_inter_tmp->desc.repeat_num != 0 ){
				id = timeval_offset_us( &tv, &p_timer->tv_base );
				id += p_inter_tmp->desc.time_us;
				p_inter_tmp->id = id;
				user_data.ull_num = id;
				p_inter_cmp = list_to_data( avl_add2( &p_timer->p_timer_avl
					, data_to_list( p_inter_tmp ), timer_cmp_func, user_data ) );
				if( p_inter_tmp != p_inter_cmp ){//already have node of id
					dlist_append( p_inter_cmp, p_inter_tmp );
				}
			}else if( ( p_inter_tmp->status & TIMER_DEL ) != 0 ){
				p_inter_tmp->status |= TIMER_STOP;
				p_inter_dels = dlist_insert( p_inter_dels, p_inter_tmp );
			}
		}
		p_timer->p_earliest_timer = list_to_data( avl_leftest_node( p_timer->p_timer_avl ) );
		pthread_mutex_unlock( &p_timer->timer_mutex );
		while( p_inter_dels != NULL ){
			p_inter_tmp = p_inter_dels;
			p_inter_dels = dlist_del( p_inter_tmp );
			p_avl = data_to_avl( data_to_list( p_inter_tmp ) );
			CALL_INTERFACE_FUNC( p_avl, pipe_interface_s, close );
			CALL_INTERFACE_FUNC( p_avl, gc_interface_s, ref_dec );
		}
		pthread_mutex_lock( &p_timer->timer_mutex );
	}
	return NULL;
}

static inline void timer_leave_avl( timer_runtime p_timer, timer_inter p_inter )
{
	timer_inter p_inter_next;

	if( ( p_inter->status & ( TIMER_STOP ) ) == 0 ){
		p_inter->status |= TIMER_STOP;
		if( dlist_prev( p_inter ) == NULL ){
			p_inter_next = dlist_next( p_inter );
			if( p_inter_next == NULL ){
				avl_del2( &p_timer->p_timer_avl, data_to_list( p_inter ) );
				if( p_inter == p_timer->p_earliest_timer ){
					p_timer->p_earliest_timer = list_to_data( 
						avl_leftest_node( p_timer->p_timer_avl ) );
				}
			}else{
				avl_replace2( &p_timer->p_timer_avl
					, data_to_list( p_inter_next ), data_to_list( p_inter ) );
				if( p_inter == p_timer->p_earliest_timer ){
					p_timer->p_earliest_timer = p_inter_next;
				}
			}
		}
		dlist_del( p_inter );
	}
}

static inline int timer_join_avl( timer_runtime p_timer, timer_inter p_inter )
{
	unsigned long long id;
	struct timeval tv;
	timer_inter p_inter_cmp;
	common_user_data_u user_data;

	gettimeofday( &tv, NULL );
	id = timeval_offset_us( &tv, &p_timer->tv_base );
	id += p_inter->desc.time_us;
#ifdef MOON_TEST
	id /= 100000;
	id *= 100000;
#endif
	p_inter->status &= ~TIMER_STOP;
	p_inter->id = id;
	user_data.ull_num = id;
	p_inter_cmp = list_to_data( avl_add2( &p_timer->p_timer_avl
		, data_to_list( p_inter ), timer_cmp_func, user_data ) );
	if( p_inter != p_inter_cmp ){
		dlist_append( p_inter_cmp, p_inter );
	}
	if( p_timer->p_earliest_timer == NULL 
		|| p_inter->id < p_timer->p_earliest_timer->id ){
		p_timer->p_earliest_timer = p_inter;
		pthread_cond_signal( &p_timer->timer_cond );
	}
	return 0;
}

static int timer_new( void * p_data, void * p_pipe, timer_desc p_desc )
{
	timer_runtime p_timer;
	timer_inter p_inter;
	pipe_interface_s * p_pipe_i;

	if( p_data == NULL || p_pipe == NULL || p_desc == NULL || p_desc->time_us == 0 ){
		MOON_PRINT_MAN( ERROR, "input parameters error!" );
		return -1;
	}
	p_timer = ( timer_runtime )p_data;
	p_inter = ( timer_inter )list_to_data( avl_to_data( p_pipe ) );
	p_pipe_i = FIND_INTERFACE( p_pipe, pipe_interface_s );
	p_inter->desc = *p_desc;
	if( p_inter->desc.repeat_num != 0 ){
		pthread_mutex_lock( &p_timer->timer_mutex );
		timer_join_avl( p_timer, p_inter );
		pthread_mutex_unlock( &p_timer->timer_mutex );
	}else{
		p_inter->status |= TIMER_STOP;
	}
	CALL_INTERFACE_FUNC( p_pipe, gc_interface_s, ref_inc );
	p_pipe_i->set_point_ref( p_pipe, p_timer );
	p_pipe_i->init_done( p_pipe, 0 );
	return 0;
}

static void timer_del( void * p_data, void * p_pipe )
{
	timer_runtime p_timer;
	timer_inter p_inter;
	int can_del;

	if( p_data == NULL || p_pipe == NULL ){
		return;
	}
	can_del = 0;
	p_timer = ( timer_runtime )p_data;
	p_inter = ( timer_inter )list_to_data( avl_to_data( p_pipe ) );
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
}

//set 失败后处于del状态
static int timer_set( void * p_data, void * p_pipe, timer_desc p_desc )
{
	timer_runtime p_timer;
	timer_inter p_inter;
	int ret;

	if( p_data == NULL || p_pipe == NULL || p_desc == NULL 
		|| p_desc->time_us == 0 || p_desc->repeat_num == 0 ){
		return -1;
	}
	ret = -1;
	p_timer = ( timer_runtime )p_data;
	p_inter = ( timer_inter )list_to_data( avl_to_data( p_pipe ) );
	pthread_mutex_lock( &p_timer->timer_mutex );
	if( ( p_inter->status & TIMER_DEL ) == 0 ){
		ret = 0;
		p_inter->desc = *p_desc;
		if( ( p_inter->status & TIMER_PROCESSING ) == 0 ){
			timer_leave_avl( p_timer, p_inter );
			timer_join_avl( p_timer, p_inter );
		}
		p_inter->status &= ~TIMER_STOP;
	}
	pthread_mutex_unlock( &p_timer->timer_mutex );
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
	p_inter = ( timer_inter )list_to_data( avl_to_data( p_pipe ) );
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
	int ret;

	if( p_data == NULL || p_pipe == NULL ){
		return -1;
	}
	ret = -1;
	p_timer = ( timer_runtime )p_data;
	p_inter = ( timer_inter )list_to_data( avl_to_data( p_pipe ) );
	pthread_mutex_lock( &p_timer->timer_mutex );
	if( ( p_inter->status & TIMER_DEL ) == 0 ){
		if( ( p_inter->status & TIMER_STOP ) == TIMER_STOP  
			&& p_inter->desc.repeat_num != 0 ){
			ret = 0;
			if( ( p_inter->status & TIMER_PROCESSING ) == 0 ){
				timer_join_avl( p_timer, p_inter );
			}
			p_inter->status &= ~TIMER_STOP;
		}
	}
	pthread_mutex_unlock( &p_timer->timer_mutex );
	return ret;
}

