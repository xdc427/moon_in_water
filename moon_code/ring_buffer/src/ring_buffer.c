#include<pthread.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/time.h>
#include<unistd.h>
#include<string.h>
#include<stdarg.h>
#include<errno.h>
#include<ctype.h>
#include"ring_buffer.h"
#include"common_interfaces.h"
#include"moon_debug.h"

#ifdef MODENAME
#undef MODENAME
#endif
#define MODENAME "ring_buffer"

//他应当有一个管道之类的东西，当检测到变化时向外输出信息，这些都应当自动生成，
//像水一样可以适配各种情况，我们需要一种手段来标记观测变量，以及标记观测变量的变化。
//当观测变量变化时，我们能用生成的代码自动编码信息，这可用附在其上的脚本来实现。
typedef struct ring_s{
	int ref_num;
	int is_destroy;
	unsigned int cur; //watch variable
	unsigned int end; //watch variable
	unsigned int length;
	unsigned int flags;
	pthread_mutex_t mutex;
	pthread_cond_t con_reader;
	pthread_cond_t con_writer;
	char buffer[ 0 ];
} ring_s;//actual data appended
typedef struct ring_s * ring;

static int ring_read( void * p_data, char *out, unsigned len, unsigned flags, ...);
static int ring_write( void * p_data, char *in, unsigned len, unsigned flags, ...);
static void ring_close( void * p_data );
static void ring_ref_inc( void * p_data );
static void ring_ref_dec( void * p_data );

STATIC_BEGAIN_INTERFACE( ring_hub )
STATIC_DECLARE_INTERFACE( gc_interface_s )
STATIC_DECLARE_INTERFACE( io_interface_s )
STATIC_END_DECLARE_INTERFACE( ring_hub, 2 )
STATIC_GET_INTERFACE( ring_hub, gc_interface_s, 0 ) = {
	.ref_inc = ring_ref_inc,
	.ref_dec = ring_ref_dec
}
STATIC_GET_INTERFACE( ring_hub, io_interface_s, 1 ) = {
	.read = ring_read,
	.write = ring_write,
	.close = ring_close
}
STATIC_END_INTERFACE( NULL )

static inline void ring_free( ring des_ring )
{
	pthread_cond_destroy( &des_ring->con_writer );
	pthread_cond_destroy( &des_ring->con_reader );
	pthread_mutex_destroy( &des_ring->mutex );
	free( GET_INTERFACE_START_POINT( des_ring ) );
}

/*
 *output: NULL fail, !NULL ok
 */
void * ring_new( unsigned int buffer_len, unsigned int flags )
{
	ring new_ring;

	if( ( new_ring = MALLOC_INTERFACE_ENTITY( sizeof( *new_ring ) + buffer_len + 1
			, 0, 0 ) ) == NULL ){
		goto malloc_error;
	}
	if( pthread_mutex_init( &new_ring->mutex, NULL ) != 0 ){
		goto mutex_error;
	}
	if( pthread_cond_init( &new_ring->con_reader, NULL ) != 0 ){
		goto con_reader_error;
	}
	if( pthread_cond_init( &new_ring->con_writer, NULL ) != 0 ){
		goto con_writer_error;
	}

	BEGAIN_INTERFACE( new_ring );
	END_INTERFACE( GET_STATIC_INTERFACE_HANDLE( ring_hub ) );

	new_ring->ref_num = 1;
	new_ring->flags = flags;
	new_ring->length = buffer_len + 1;
	//trigger here
	new_ring->cur = 0;
	new_ring->end = 0;
	new_ring->is_destroy = 0;
	return new_ring;

con_writer_error:
	pthread_cond_destroy( &new_ring->con_reader );
con_reader_error:
	pthread_mutex_destroy( &new_ring->mutex );
mutex_error:
	free( new_ring );
malloc_error:
	return NULL;
}

static inline void tv_addup( struct timeval * dst, struct timeval * src )
{
	dst->tv_sec += src->tv_sec;
	dst->tv_usec += src->tv_usec;
	if( dst->tv_usec >= 1000000 ){
		dst->tv_sec += dst->tv_usec / 1000000;
		dst->tv_usec %= 1000000;
	}
}

/*
 *output: tv1 == tv2  0, tv1 > tv2  > 0, tv1 < tv2  < 0
 */
static inline int tv_compare( struct timeval * tv1, struct timeval * tv2 )
{
	if( tv1->tv_sec > tv2->tv_sec ){
		return 1;
	}else if( tv1->tv_sec < tv2->tv_sec ){
		return -1;
	}else{
		if( tv1->tv_usec > tv2->tv_usec ){
			return 1;
		}else if( tv1->tv_usec < tv2->tv_usec ){
			return -1;
		}else{
			return 0;
		}
	}
}

/*
 *output: -1 error, >= 0 ok
 */
static int ring_read( void * p_data, char *out, unsigned len, unsigned flags, ...)
{
	ring cur_ring;
	struct timeval tv, now;
	struct timespec ts;
	int peek_skip = 0;
	va_list val;
	int already_read = 0;
	int cur_can_read = 0;
	int really_read = 0;
	int cur_need_read = 0;
	int out_len = len;
	int tmp, ret, tmp_skip;
	char * buffer;

	if( p_data == 0 || out == NULL || len == 0 ){
		return 0;
	}
	cur_ring = ( ring )p_data;
	va_start( val, flags );
	if( flags & RING_TIMEOUT ){
		tv = *va_arg( val, struct timeval * );
		gettimeofday( &now, NULL );
		tv_addup( &tv, &now );
	}
	if( flags & RING_PEEK ){
		peek_skip = va_arg( val, int );
		if( peek_skip < 0 || peek_skip >= cur_ring->length - 1 ){
			return 0;
		}
		if( ( flags & RING_MSGMODE ) && out_len > cur_ring->length -1 - peek_skip ){
			return 0;
		}
		if( out_len > cur_ring->length - 1 - peek_skip ){
			out_len = cur_ring->length - 1 - peek_skip;
		}
	}
	va_end( val );

	buffer = cur_ring->buffer;
	pthread_mutex_lock( &cur_ring->mutex );
	while( 1 ){
		tmp = cur_ring->length - cur_ring->cur;
		tmp_skip = tmp - peek_skip;
		cur_can_read = ( ( tmp + cur_ring->end ) % cur_ring->length ) - peek_skip;
		cur_need_read = out_len - already_read;
		really_read = cur_need_read > cur_can_read ? cur_can_read : cur_need_read;
		if( flags & RING_MSGMODE && really_read < cur_need_read ){
			really_read = 0;
		}
		if( really_read  > 0 ){
			if( cur_ring->end < cur_ring->cur && tmp > peek_skip && tmp_skip < really_read ){
				memcpy( out + already_read, buffer + cur_ring->cur + peek_skip, tmp_skip );
				memcpy( out + already_read + tmp_skip, buffer, really_read - tmp_skip );
			}else{
				memcpy( out + already_read, buffer + cur_ring->cur + peek_skip, really_read );
			}
#ifdef MOON_TEST
			do{
				int i;
				char c;
				char * str;
				str = out + already_read;
				for( i = 0; i < really_read && isprint( str[ i ] ); i++ )
					;
				if( i != 0 ){
					CUT_STRING( str, i, c );
					MOON_PRINT( TEST, "ring_read", "%s", str );
					RECOVER_STRING( str, i, c );
				}
			}while( 0 );
#endif

			already_read += really_read;
			if( ( flags & RING_PEEK ) == 0 ){
				cur_ring->cur += really_read;
				cur_ring->cur %= cur_ring->length;
				pthread_cond_signal( &cur_ring->con_writer );
			}
		}
		if( cur_can_read > cur_need_read ){
			pthread_cond_signal( &cur_ring->con_reader );
			pthread_mutex_unlock( &cur_ring->mutex );
			return already_read;
		}else if( cur_can_read == cur_need_read ){
			goto read_over;
		}else{
			if( cur_ring->is_destroy == 1 ){
				pthread_mutex_unlock( &cur_ring->mutex );
				return already_read > 0 ? already_read : -1;
			}
			if( ( ( flags & RING_WAITALL ) == 0 && already_read > 0 ) 
					|| ( ( flags & RING_NOWAIT ) || ( cur_ring->flags & RING_NOWAIT ) ) ){
				goto read_over;
			}
			if( flags & RING_TIMEOUT ){
				gettimeofday( &now, NULL );
				if( tv_compare( &now, &tv ) >= 0 ){
					 goto read_over;
				}
				ts.tv_sec = tv.tv_sec;
				ts.tv_nsec = tv.tv_usec * 1000;
				ret = pthread_cond_timedwait( &cur_ring->con_reader, &cur_ring->mutex, &ts );
				if( ret == ETIMEDOUT ){
					goto read_over;
				}
			}else{
				pthread_cond_wait( &cur_ring->con_reader, &cur_ring->mutex );
			}
		}
	}
read_over:
	if( ( flags & RING_PEEK ) && cur_ring->end != cur_ring->cur ){
		pthread_cond_signal( &cur_ring->con_reader );
	}
	pthread_mutex_unlock( &cur_ring->mutex );
	return already_read;
}

/*
 *output: -1 error, >= 0 ok
 */
static int ring_write( void * p_data, char *in, unsigned len, unsigned flags, ...)
{
	ring cur_ring;
	va_list va;
	struct timeval tv, now;
	struct timespec ts;
	int already_write = 0;
	int cur_can_write = 0;
	int cur_need_write = 0;
	int really_write = 0;
	int tmp, ret;
	char *buffer;

	if( p_data == NULL || in == NULL || len <= 0 ){
		return 0;
	}
	cur_ring = ( ring )p_data;
	if( ( flags & RING_MSGMODE ) && cur_ring->length - 1 < len ){
		return 0;;
	}
	va_start( va, flags );
	if( flags & RING_TIMEOUT ){
		gettimeofday( &now, NULL );
		tv = *va_arg( va, struct timeval * );
		tv_addup( &tv, &now );
	}
	va_end( va );

	buffer = cur_ring->buffer;
	pthread_mutex_lock( &cur_ring->mutex );
	while(1){
		if( cur_ring->is_destroy == 1 ){
			pthread_mutex_unlock( &cur_ring->mutex );
			return already_write > 0 ? already_write : -1;
		}
		tmp = cur_ring->length - cur_ring->end;
		cur_need_write = len - already_write;
		cur_can_write = ( tmp + cur_ring->cur - 1 ) % cur_ring->length ;
		if( flags & RING_REPLACE_OLD ){
			if( cur_need_write > cur_ring->length - 1 ){
				already_write += cur_need_write - cur_ring->length + 1;
				cur_need_write = cur_ring->length - 1;
			}
			really_write = cur_need_write;
		}else{
			really_write = cur_need_write > cur_can_write ? cur_can_write : cur_need_write;
		}
		if( flags & RING_MSGMODE && really_write < cur_need_write ){
			really_write = 0;
		}
		if( really_write > 0 ){
			if( cur_ring->end >= cur_ring->cur && tmp < really_write ){
				memcpy( buffer + cur_ring->end, in + already_write, tmp );
				memcpy( buffer, in + already_write + tmp, really_write - tmp );
			}else{
				memcpy( buffer + cur_ring->end, in + already_write, really_write );
			}

#ifdef MOON_TEST
			do{
				int i;
				char c;
				char * str;
				str = in + already_write;
				for( i = 0; i < really_write && isprint( str[ i ] ); i++ )
					;
				if( i != 0 ){
					CUT_STRING( str, i, c );
					MOON_PRINT( TEST, "ring_write", "%s", str );
					RECOVER_STRING( str, i, c );
				}
			}while( 0 );
#endif
			already_write += really_write;
			cur_ring->end += really_write;
			cur_ring->end %= cur_ring->length;
			if( really_write > cur_can_write ){
				cur_ring->cur += really_write - cur_can_write;
				cur_ring->cur %= cur_ring->length;
			}
			pthread_cond_signal( &cur_ring->con_reader );
		}
		if( cur_can_write > cur_need_write ){
			pthread_cond_signal( &cur_ring->con_writer );
			pthread_mutex_unlock( &cur_ring->mutex );
			return already_write;
		}else if( cur_can_write == cur_need_write ){
			pthread_mutex_unlock( &cur_ring->mutex );
			return already_write;
		}else{
			if( ( flags & RING_REPLACE_OLD )  //already write before
					|| ( ( flags & RING_NOWAIT ) || ( cur_ring->flags & RING_NOWAIT ) )
					|| ( ( flags & RING_WAITALL ) == 0 && already_write > 0 ) ){
				pthread_mutex_unlock( &cur_ring->mutex );
				return already_write;
			}
			if( flags & RING_TIMEOUT ){
				gettimeofday( &now, NULL );
				if( tv_compare( &tv, &now ) >= 0 ){
					pthread_mutex_unlock( &cur_ring->mutex );
					return already_write;
				}
				ts.tv_sec = tv.tv_sec;
				ts.tv_nsec = tv.tv_usec * 1000;
				ret = pthread_cond_timedwait( &cur_ring->con_writer, &cur_ring->mutex, &ts );
				if( ret == ETIMEDOUT ){
					pthread_mutex_unlock( &cur_ring->mutex );
					return already_write;
				}
			}else{
				pthread_cond_wait( &cur_ring->con_writer, &cur_ring->mutex );
			}
		}
	}
}

static void ring_close( void * p_data )
{
	ring p_ring;

	p_ring = ( ring )p_data;
	pthread_mutex_lock( &p_ring->mutex );
	if( p_ring->is_destroy == 0 ){
		p_ring->is_destroy = 1;
		pthread_cond_broadcast( &p_ring->con_reader );
		pthread_cond_broadcast( &p_ring->con_writer );
	}
	pthread_mutex_unlock( &p_ring->mutex );
}

static void ring_ref_inc( void * p_data )
{
	GC_REF_INC( ( ring )p_data  );
}

static void ring_ref_dec( void * p_data )
{
	ring p_ring;
	int ref_num;

	p_ring = ( ring )p_data;
	ref_num = GC_REF_DEC( p_ring );
	if( ref_num > 0 ){
		return;
	}else if( ref_num == 0 ){
		if( p_ring->is_destroy == 0 ){
			MOON_PRINT_MAN( ERROR, "ring buffer not closed when free!" );
		}
		ring_free( p_ring );
	}else{
		MOON_PRINT_MAN( ERROR, "ref num under overflow!" );
	}
}

