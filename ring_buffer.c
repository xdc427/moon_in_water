#include<pthread.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/time.h>
#include<unistd.h>
#include<string.h>
#include<stdarg.h>
#include<errno.h>
#include"ring_buffer.h"
#include"debug/moon_debug.h"

#define LONG_BITS ( sizeof( long ) << 3 )
enum{ 
	RING_MAX = 1024
};

//他应当有一个管道之类的东西，当检测到变化时向外输出信息，这些都应当自动生成，
//像水一样可以适配各种情况，我们需要一种手段来标记观测变量，以及标记观测变量的变化。
//当观测变量变化时，我们能用生成的代码自动编码信息，这可用附在其上的脚本来实现。
struct ring_s{
	pthread_mutex_t mutex;
	pthread_cond_t con_reader;
	pthread_cond_t con_writer;
	unsigned int cur; //watch variable
	unsigned int end; //watch variable
	unsigned int length;
	unsigned int flags;
	int is_destroy;
};//actual data appended
typedef struct ring_s ring_s;
typedef struct ring_s * ring;

struct ring_quote_s{
	unsigned long quote_num;
	ring ring;
};
typedef struct ring_quote_s ring_quote_s;
typedef struct ring_quote_s * ring_quote;

static ring_quote_s quoter[ RING_MAX ] = { { 0 } };
static pthread_mutex_t mutex_map = PTHREAD_MUTEX_INITIALIZER;

/*
 * output: -1 fail, >= 0 ok
 */ 
static int get_free_ring( ring_quote quoter, unsigned int len )
{
	int i = 0;
	
	for( i = 0; i < len; i++ ){
		if( quoter->quote_num == 0 ){
			return i;
		}
	}
	return -1;
}

static ring ring_init( unsigned buffer_len, unsigned int flags )
{
	ring new_ring = NULL;

	if( ( new_ring = malloc( sizeof( *new_ring ) + buffer_len ) ) == NULL ){
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
	new_ring->flags = flags;
	new_ring->length = buffer_len;
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
	if( new_ring != NULL ){
		free( new_ring );
	}
malloc_error:
	return NULL;
}

static void ring_free( ring des_ring )
{
	if( des_ring == NULL ){
		return;
	}
	pthread_cond_destroy( &des_ring->con_writer );
	pthread_cond_destroy( &des_ring->con_reader );
	pthread_mutex_destroy( &des_ring->mutex );
	//printf("free ok\n");
	free( des_ring );
}

/*
 *output: -1 fail, >= 0 ok
 */
int ring_new( unsigned int buffer_len, unsigned int flags )
{
	int index = -1;
	ring new_ring = NULL;
	
	if( buffer_len <= 0 ){
		return -1;
	}

	pthread_mutex_lock( &mutex_map );
	index = get_free_ring( quoter, RING_MAX );
	pthread_mutex_unlock( &mutex_map );
	if( index < 0 || ( new_ring = ring_init( buffer_len, flags ) ) == NULL ){
		return -1; 
	}   
	pthread_mutex_lock( &mutex_map );
	if( quoter[ index ].quote_num != 0
			&& ( index = get_free_ring( quoter, RING_MAX ) ) == -1 ){
		pthread_mutex_unlock( &mutex_map );
		ring_free( new_ring );
		return -1;
	}
	quoter[ index ].quote_num = 1;
	quoter[ index ].ring = new_ring;
	pthread_mutex_unlock( &mutex_map );
	return index;
}

/*
 *output: -1 error, >=0 ok
 *input: fd 要加入的ring句柄
 */
int ring_join( int fd )
{
	if( fd < 0 || fd >= RING_MAX ){
		return -1;
	}
	pthread_mutex_lock( &mutex_map );
	if( quoter[ fd ].quote_num == 0 ){
		pthread_mutex_unlock( &mutex_map );
		return -1;
	}
	quoter[ fd ].quote_num++;
	pthread_mutex_unlock( &mutex_map );
	return fd;
}

void ring_leave( int fd, int is_destroy )
{
	ring des_ring = NULL;

	if( fd < 0 || fd >= RING_MAX ){
		return;
	}
	if( is_destroy ){
		pthread_mutex_lock( &mutex_map );
		switch( quoter[ fd ].quote_num ){
		case 0:
			pthread_mutex_unlock( &mutex_map );
			return;
		case 1: //need delete it
			des_ring = quoter[ fd ].ring ;
			quoter[ fd ].ring = NULL;
			quoter[ fd ].quote_num = 0;
			pthread_mutex_unlock( &mutex_map );
			ring_free( des_ring );
			return;
		default:
			des_ring = quoter[ fd ].ring;
			pthread_mutex_unlock( &mutex_map );
		}
		pthread_mutex_lock( &des_ring->mutex );
		des_ring->is_destroy = 1;
		pthread_cond_broadcast( &des_ring->con_reader );
		pthread_cond_broadcast( &des_ring->con_writer );
		pthread_mutex_unlock( &des_ring->mutex );
	}

	pthread_mutex_lock( &mutex_map );
	switch( quoter[ fd ].quote_num ){
	case 0:
		//if is_destroy ,then panic error !!!!!
		pthread_mutex_unlock( &mutex_map );
		return;
	case 1:
		des_ring = quoter[ fd ].ring;
		quoter[ fd ].ring = NULL;
		quoter[ fd ].quote_num = 0;
		pthread_mutex_unlock( &mutex_map );
		ring_free( des_ring );
		return;
	default:
		quoter[ fd ].quote_num--;
		pthread_mutex_unlock( &mutex_map );

	}
}

static void tv_addup( struct timeval * dst, struct timeval * src )
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
static int tv_compare( struct timeval * tv1, struct timeval * tv2 )
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

static ring ring_search( int fd )
{
	ring tmp = NULL;

	if( fd < 0 || fd >= RING_MAX ){
		return NULL;
	}
	pthread_mutex_lock( &mutex_map );
	if( quoter[ fd ].quote_num != 0 ){
		tmp = quoter[ fd ].ring;
	}
	pthread_mutex_unlock( &mutex_map );
	return tmp;
}

/*
 *output: -1 error, >= 0 ok
 */
int ring_read( int fd, char *out, unsigned int len, unsigned int flags, ...)
{
	ring cur_ring = NULL;
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

	if( fd < 0 || fd >= RING_MAX || out == NULL || len == 0 )
		return 0;
	if( ( cur_ring = ring_search( fd ) ) == NULL ){
		return -1;
	}
	va_start( val, flags );
	if( flags & RING_TIMEOUT ){
		tv = *va_arg( val, struct timeval * );
		gettimeofday( &now, NULL );
		tv_addup( &tv, &now );
	}
	if( flags & RING_PEEK ){
		peek_skip = va_arg( val, int );
		if( peek_skip < 0 || peek_skip >= cur_ring->length-1 ){
			return 0;
		}
		if( out_len > cur_ring->length - 1 - peek_skip ){
			out_len = cur_ring->length - 1 - peek_skip;
		}
	}
	va_end( val );

	buffer = ( char * )( cur_ring + 1 );
	pthread_mutex_lock( &cur_ring->mutex );
	while( 1 ){
		tmp = cur_ring->length - cur_ring->cur;
		tmp_skip = tmp - peek_skip;
		cur_can_read = ( ( tmp + cur_ring->end ) % cur_ring->length ) - peek_skip;
		cur_need_read = out_len - already_read;
		really_read = cur_need_read > cur_can_read ? cur_can_read : cur_need_read;
		if( really_read  > 0 ){
			if( cur_ring->end < cur_ring->cur && tmp > peek_skip && tmp_skip < really_read ){
				memcpy( out + already_read, buffer + cur_ring->cur + peek_skip, tmp_skip );
				memcpy( out + already_read + tmp_skip, buffer, really_read - tmp_skip );
			}else{
				memcpy( out + already_read, buffer + cur_ring->cur + peek_skip, really_read );
			}
			//test
			//gettimeofday( &now, NULL );
			//*( out + already_read + really_read ) = 0;
			//fprintf( stdout, "%ld%06ld:%s\n", now.tv_sec, now.tv_usec, out + already_read );
			//fflush( stdout );

			//test
			MOON_TEST(
				*( out + already_read + really_read ) = 0;
				MOON_PRINT( TEST, "ring_read", "%s", out + already_read );
			)

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
int ring_write( int fd, char *in, unsigned int len, unsigned int flags, ...)
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

	if( fd < 0 || fd >= RING_MAX || in == NULL || len <= 0 ){
		return 0;
	}
	if( ( cur_ring = ring_search( fd ) ) == NULL ){
		return -1;
	}
	va_start( va, flags );
	if( flags & RING_TIMEOUT ){
		gettimeofday( &now, NULL );
		tv = *va_arg( va, struct timeval * );
		tv_addup( &tv, &now );
	}
	va_end( va );

	buffer = ( char * )( cur_ring + 1 );
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
		//printf( "cur:end:cur_need_write:cur_can_write:really_write: > %d:%d:%d:%d:%d\n"
		//	, cur_ring->cur, cur_ring->end, cur_need_write, cur_can_write, really_write );
		if( really_write > 0 ){
			if( cur_ring->end >= cur_ring->cur && tmp < really_write ){
				//printf( "cp:%d-%d\n", cur_ring->end, cur_ring->end + tmp );
				memcpy( buffer + cur_ring->end, in + already_write, tmp );
				//printf( "cp:%d-%d\n", 0, really_write - tmp );
				memcpy( buffer, in + already_write + tmp, really_write - tmp );
			}else{
				//printf( "cp:%d-%d\n", cur_ring->end, cur_ring->end + really_write );
				memcpy( buffer + cur_ring->end, in + already_write, really_write );
			}

			/*test
			
			char c;
			gettimeofday( &now, NULL );
			c = *( in + already_write + really_write );
			*( in + already_write + really_write ) = 0;
			fprintf( stderr, "%ld%06ld:%s\n", now.tv_sec, now.tv_usec, in + already_write );
			fflush( stderr );
			*( in +already_write + really_write ) = c;
		
			test*/
			MOON_TEST(
				char c;
				c = *( in + already_write + really_write );
				*( in + already_write + really_write ) = 0;
				MOON_PRINT( TEST, "ring_write", "%s", in + already_write );
				*( in +already_write + really_write ) = c;
			)
			already_write += really_write;
			//printf( "already_write:%d\n", already_write );
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

