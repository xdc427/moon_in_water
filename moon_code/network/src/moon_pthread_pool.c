#include<stdio.h>
#include<pthread.h>
#include"ring_buffer.h"
#include"moon_debug.h"
#include"moon_pthread_pool.h"
#include"common_interfaces.h"

#ifdef MODENAME
#undef MODENAME
#endif
#define MODENAME "moon_pthread_pool" 

typedef struct process_pool_s{
	int status;
	int process_max;//>=1
	int idle_max;//>=1
	int idle_time;//ms
	int process_num;
	int task_num;
	void * ring_fd;
	io_interface_s * p_ring_i;
	gc_interface_s * p_gc_i;
	pthread_mutex_t process_mutex;
} process_pool_s;
typedef process_pool_s * process_pool;

enum{
	PROCESS_MAX_DEFAULT = 64,
	IDLE_MAX_DEFAULT = 8,
	IDLE_TIME_DEFAULT = 100000
};

static int put_task( void * p_data, process_task p_task );
static void get_task_info( void * p_data, int * task_num, int * process_num );

STATIC_BEGAIN_INTERFACE( pthreadpool_hub )
STATIC_DECLARE_INTERFACE( pthreadpool_interface_s )
STATIC_END_DECLARE_INTERFACE( pthreadpool_hub, 1, process_pool_s ppool )
STATIC_GET_INTERFACE( pthreadpool_hub, pthreadpool_interface_s, 0 ) = {
	.put_task = put_task,
	.get_task_info = get_task_info
}
STATIC_INIT_USERDATA( ppool ) = {
	.process_max = PROCESS_MAX_DEFAULT,
	.idle_max = IDLE_MAX_DEFAULT,
	.idle_time = IDLE_TIME_DEFAULT,
	.process_mutex = PTHREAD_MUTEX_INITIALIZER
}
STATIC_END_INTERFACE( NULL )

static void * process_func( void * arg )
{
	process_pool p_ppool;
	process_task_s task;
	struct timeval tv;
	int quit, ret;

	p_ppool = ( process_pool )arg;
	tv.tv_sec = p_ppool->idle_time / 1000;
	tv.tv_usec = ( p_ppool->idle_time % 1000 ) * 1000;
	for( quit = 0; quit == 0; ){
		ret = p_ppool->p_ring_i->read( p_ppool->ring_fd, ( char * )&task, sizeof( task )
				, RING_MSGMODE | RING_TIMEOUT, &tv );
		switch( ret ){
		case sizeof( task ):
			task.task_func( task.user_data );
			pthread_mutex_lock( &p_ppool->process_mutex );
			p_ppool->task_num--;
			pthread_mutex_unlock( &p_ppool->process_mutex );
			break;
		case 0:
			pthread_mutex_lock( &p_ppool->process_mutex );
			if( p_ppool->process_num > p_ppool->task_num + p_ppool->idle_max ){
				p_ppool->process_num--;
				quit = 1;
			}
			pthread_mutex_unlock( &p_ppool->process_mutex );
			if( quit ){
				p_ppool->p_gc_i->ref_dec( p_ppool->ring_fd );
			}
			break;
		default:
			MOON_PRINT_MAN( ERROR
				, "ret:%d,need:%d,fatal error: ring buffer read error!", ret, ( int )sizeof( task ) );
			quit = 1;
			p_ppool->p_ring_i->close( p_ppool->ring_fd );
			p_ppool->p_gc_i->ref_dec( p_ppool->ring_fd );
			pthread_mutex_lock( &p_ppool->process_mutex );
			p_ppool->process_num--;
			pthread_mutex_unlock( &p_ppool->process_mutex );
		}
	}
	return NULL;
}

static int _put_task( process_pool p_ppool, process_task p_task )
{
	int ret, new;
	pthread_t pthread;
	pthread_attr_t attr;

	new = 0;
	pthread_mutex_lock( &p_ppool->process_mutex );
	p_ppool->task_num++;
	if( p_ppool->process_num < p_ppool->task_num && p_ppool->process_num < p_ppool->process_max ){
		p_ppool->process_num++;
		new = 1;
	}
	pthread_mutex_unlock( &p_ppool->process_mutex );
	if( new ){
		p_ppool->p_gc_i->ref_inc( p_ppool->ring_fd );
		pthread_attr_init( &attr );
		pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
		ret = pthread_create( &pthread, &attr, process_func, p_ppool );
		if( ret != 0 ){
			pthread_mutex_lock( &p_ppool->process_mutex );
			p_ppool->process_num--;
			pthread_mutex_unlock( &p_ppool->process_mutex );
			p_ppool->p_gc_i->ref_dec( p_ppool->ring_fd );
		}
		pthread_attr_destroy( &attr );	
	}
	ret = p_ppool->p_ring_i->write( p_ppool->ring_fd
		, ( char * )p_task, sizeof( *p_task ), RING_NOWAIT | RING_MSGMODE );
	if( ret != sizeof( *p_task ) ){
		pthread_mutex_lock( &p_ppool->process_mutex );
		p_ppool->task_num--;
		pthread_mutex_unlock( &p_ppool->process_mutex );
		return -1;
	}
	return 0;
}

static int put_task( void * p_data, process_task p_task )
{
	if( p_data == NULL || p_task == NULL ){
		return -1;
	}
	return 	_put_task( ( process_pool )p_data, p_task );
}

static void process_pool_start( process_pool p_ppool )
{
	int i;
	pthread_t pthread;
	pthread_attr_t attr;

	pthread_attr_init( &attr );
	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
	for( i = 0; i < p_ppool->idle_max; i++ ){
		p_ppool->process_num++;
		p_ppool->p_gc_i->ref_inc( p_ppool->ring_fd );
		if( pthread_create( &pthread, &attr, process_func, p_ppool ) != 0 ){
			p_ppool->process_num--;
			p_ppool->p_gc_i->ref_dec( p_ppool->ring_fd );
		}
	}
	pthread_attr_destroy( &attr );
}

void * get_pthreadpool_instance()
{
	process_pool p_ppool;

	p_ppool = &pthreadpool_hub.ppool;
	if( __builtin_expect( p_ppool->status == 0, 0 ) ){
		pthread_mutex_lock( &p_ppool->process_mutex );
		if( p_ppool->status == 0 ){
			p_ppool->ring_fd = ring_new( sizeof( process_task_s ) * p_ppool->process_max * 10, 0 );
			if( p_ppool->ring_fd != NULL ){
				p_ppool->p_ring_i = FIND_INTERFACE( p_ppool->ring_fd, io_interface_s );
				p_ppool->p_gc_i = FIND_INTERFACE( p_ppool->ring_fd, gc_interface_s );
				process_pool_start( p_ppool );
				p_ppool->status = 1;
			}else{
				MOON_PRINT_MAN( ERROR, "create process pool's ring buffer error!" );
				p_ppool = NULL;
			}
		}
		pthread_mutex_unlock( &p_ppool->process_mutex );
	}
	return p_ppool;
}

static void get_task_info( void * p_data, int * task_num, int * process_num )
{
	process_pool p_ppool;

	p_ppool = ( process_pool )p_data;

	if( p_data == NULL || task_num == NULL || process_num == NULL ){
		return;
	}
	pthread_mutex_lock( &p_ppool->process_mutex );
	*task_num = p_ppool->task_num;
	*process_num = p_ppool->process_num;
	pthread_mutex_unlock( &p_ppool->process_mutex );
}

