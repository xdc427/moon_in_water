#include<sys/time.h>
#include<stdio.h>
#include<pthread.h>
#include<time.h>
#include<stdlib.h>
#include<signal.h>
#include<string.h>
#include"ring_buffer.h"
#include"common_interfaces.h"

const char base[] = "abcdefghjkmnpqrstwxyz23456789";
int worker_num_g;
int total_bytes = 0;

void dump()
{
	char buf[ 1024 ];
	char cmd[ 1024 ];
	FILE * fp;

	snprintf( buf, sizeof( buf ), "/proc/%d/cmdline", getpid() );
	if( ( fp = fopen( buf, "r" ) ) == NULL ){
		exit( 0 );
	}
	buf[0] = 0;
	fgets( buf, sizeof( buf ), fp );
	fclose( fp );
	if( buf[ strlen( buf ) - 1 ] == '\n' ){
		buf[ strlen( buf ) - 1 ] = 0;
	}
	snprintf( cmd, sizeof( cmd ), "gdb %s %d", buf, getpid() );
	system( cmd );
//	exit( 0 );
}

void * worker_fun( void * arg )
{
	int num = 640;
	int len = 1600;
	char * buffer = NULL;
	int i, j, ret;
	unsigned int flags = 0;
	struct timeval tv;
	gc_interface_s * p_gc_i;
	io_interface_s * p_ring_i;
	
	gettimeofday( &tv, NULL );
	srand( tv.tv_usec );
	flags = ( rand() % 2 ) | ( ( rand() % 2 ) << 2 ) | ( ( rand() % 2 ) << 4 );
	tv.tv_sec = rand() % 10;
	num = rand() % 100;
	len = rand() % ( 1024 * 1024 );
	len++;
	
	p_ring_i = FIND_INTERFACE( arg, io_interface_s );
	p_gc_i = FIND_INTERFACE( arg, gc_interface_s );
	buffer = malloc( len + 1 );
	for( i = 0; i < num; i++ ){
		for( j = 0; j < len; j++ ){
			buffer[ j ] = base[ rand() % ( sizeof( base ) - 1 ) ];
		}
		ret = p_ring_i->write( arg, buffer, len, flags, &tv );
		//printf( "write num:%d\n", ret );
	}
	p_gc_i->ref_dec( arg );
	ret = __sync_sub_and_fetch( &worker_num_g, 1 );
	if( ret == 0 ){
		p_ring_i->close( arg );
	}
	if( buffer != NULL ){
		free( buffer );
	}
	return NULL;
}

void * consumer_fun( void * arg)
{
	char *buffer = NULL;
	int ret;
	unsigned int flags = 0;
	struct timeval tv;
	int buffer_len;
	gc_interface_s * p_gc_i;
	io_interface_s * p_ring_i;
	
	gettimeofday( &tv, NULL );
	srand( tv.tv_usec );
	flags = ( rand() % 2 ) | ( ( rand() % 2 ) << 2 ) | ( ( rand() % 2 ) << 4 );
	tv.tv_sec = rand() % 10;
	buffer_len = rand() % ( 1024 * 1024 );
	buffer_len += 2 ;
	buffer = malloc( buffer_len );
	p_ring_i = FIND_INTERFACE( arg, io_interface_s );
	p_gc_i = FIND_INTERFACE( arg, gc_interface_s );
	
	while(1){
		if( ( ret = p_ring_i->read( arg, buffer, buffer_len - 1, flags, &tv ) ) == -1 
			|| ( ret == 0 && worker_num_g == 0 ) ){
			free( buffer );
			p_gc_i->ref_dec( arg );
			return NULL;
		}
		buffer[ ret ] = '\0';
		__sync_add_and_fetch( &total_bytes, ret );
	}
}

void main()
{
	pthread_t * thread;
	void *status;
	int worker_num = 0;
	int consumer_num = 0;
	int i, ret, time;
	void * ring_fd;
	gc_interface_s * p_gc_i;
	io_interface_s * p_ring_i;
	struct timeval tv, tv2; 
	double speed;
	
//	signal( SIGSEGV, &dump );
	gettimeofday( &tv, NULL );
	srand( tv.tv_usec );
	ring_fd = ring_new( 1 + ( rand() % ( 1024 * 1024 ) ), 0 );

	p_ring_i = FIND_INTERFACE( ring_fd, io_interface_s );
	p_gc_i = FIND_INTERFACE( ring_fd, gc_interface_s );	
	worker_num = 1 + ( rand() % 50 );
	consumer_num = 1 + ( rand() % 50 );
	thread = calloc( sizeof( pthread_t ), worker_num + consumer_num );
	worker_num_g = worker_num;
	gettimeofday( &tv, NULL );
	for( i = 0; i < worker_num + consumer_num; i++ ){
		p_gc_i->ref_inc( ring_fd );
		if( pthread_create( &thread[ i ], NULL
				, i < worker_num ? worker_fun : consumer_fun , ring_fd ) != 0 ){
			p_gc_i->ref_dec( ring_fd );
			ret = __sync_sub_and_fetch( &worker_num_g, 1 );
			if( ret == 0 ){
				p_ring_i->close( ring_fd );
			}
		}
	}
	for( i = 0; i < worker_num + consumer_num; i++ ){
		pthread_join( thread[i], &status );
	}
	gettimeofday( &tv2, NULL );
	time = ( tv2.tv_sec - tv.tv_sec ) * 1000000 + tv2.tv_usec - tv.tv_usec;
	speed = ( double )total_bytes/ time * 2;
	fprintf( stderr, "%.2lf\n", speed );
	p_ring_i->close( ring_fd );
	p_gc_i->ref_dec( ring_fd );
}

