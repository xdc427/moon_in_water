#include"moon_thread_info.h"
#include"moon_common.h"

static pthread_key_t thread_info_key;
static pthread_once_t once = PTHREAD_ONCE_INIT;

static void del_thread_info( void * p_data )
{
	IF_FREE( p_data );
}

static void init_thread_info()
{
	if( pthread_key_create( &thread_info_key, del_thread_info ) !=  0 ){
		MOON_PRINT_MAN( ERROR, "thread info key create error!" );
	}
}

int set_thread_info( thread_info p_info )
{
	pthread_once( &once, init_thread_info );
	if( pthread_setspecific( thread_info_key, ( void * )p_info ) == 0 ){
		return 0;
	}
	MOON_PRINT_MAN( ERROR, "set thread key info error!" );
	return -1;
}

thread_info get_thread_info( )
{
	pthread_once( &once, init_thread_info );
	return ( thread_info )pthread_getspecific( thread_info_key );
}

thread_info init_thread()
{
	thread_info p_info;

	p_info = get_thread_info();
	if( p_info != NULL ){
		return p_info;
	}
	p_info = malloc( sizeof( *p_info ) );
	if( p_info != NULL ){
		if( set_thread_info( p_info ) < 0 ){
			free( p_info );
			p_info = NULL;
		}
	}
	return p_info;
}

