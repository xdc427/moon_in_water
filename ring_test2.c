#include<stdio.h>
#include<pthread.h>
#include<time.h>
#include<stdlib.h>
#include<signal.h>
#include<string.h>
#include"ring_buffer.h"

const char base[] = "abcdefghjkmnpqrstwxyz23456789";
int ring_fd = -1;

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
	int num = 2;
	int len = 100;
	char * buffer = NULL;
	int i, j, ret;
	

	ring_join( ring_fd );

	buffer = malloc( len + 1 );
	for( i = 0; i < num; i++ ){
		for( j = 0; j < len; j++ ){
			buffer[ j ] = base[ rand() % ( sizeof( base ) - 1 ) ];
		}
		ret = ring_write( ring_fd, buffer, len, RING_REPLACE_OLD );
		//printf( "write num:%d\n", ret );
	}
	ring_leave( ring_fd, 1 );
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
	
	buffer_len = 8 ;
	buffer = malloc( buffer_len );
	ring_join( ring_fd );
	sleep(10);
	while(1){
		if( ( ret = ring_read( ring_fd, buffer, buffer_len - 1, 0 ) )== -1 ){
			free( buffer );
			ring_leave( ring_fd, 0 );
			return;
		}
		buffer[ ret ] = '\0';
	}
}

void main()
{
	pthread_t * thread;
	void *status;
	struct timeval tv;
	int worker_num = 0;
	int consumer_num = 0;
	int i;

//	signal( SIGSEGV, &dump );
	gettimeofday( &tv, NULL );
	srand( tv.tv_usec );
	ring_fd = ring_new( 1 + 8, 0 );

	worker_num = 1 ;
	consumer_num = 1;
	thread = calloc( sizeof( pthread_t ), worker_num + consumer_num );
	for( i = 0; i < worker_num + consumer_num; i++ ){
		pthread_create( &thread[ i ], NULL, i < worker_num ? worker_fun : consumer_fun , NULL );
	}
	for( i = 0; i < worker_num + consumer_num; i++ ){
		pthread_join( thread[i], &status );
	}
	ring_leave( ring_fd, 0 );
}

