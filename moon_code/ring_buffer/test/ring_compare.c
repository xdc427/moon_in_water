#include<stdio.h>
#include<sys/time.h>
#include<stdlib.h>

const char base[] = "abcdefghjkmnpqrstwxyz23456789";

void main()
{
	char * buffer = NULL;
	int i, j, time;
	struct timeval tv, tv2;
	double speed;
	int num, len;
		
	gettimeofday( &tv, NULL );
	srand( tv.tv_usec );
	num = 1024 * 4;
	len = 512 * 1024;
	
	buffer = malloc( len );
	for( i = 0; i < num; i++ ){
		for( j = 0; j < len; j++ ){
			buffer[ j ] = base[ rand() % ( sizeof( base ) - 1 ) ];
		}
		buffer[ len - 1 ] = '\0';
		printf( "%s\n", buffer );
	}
	fflush( stdout );
	gettimeofday( &tv2, NULL );
	time = ( tv2.tv_sec - tv.tv_sec ) * 1000000 + tv2.tv_usec - tv.tv_usec;
	speed = ( double )num * len / time;
	fprintf( stderr, "%.2lf\n", speed );
	if( buffer != NULL ){
		free( buffer );
	}
}

