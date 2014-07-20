#include<sys/time.h>
#include<stdio.h>

void main()
{
	struct timeval tv1, tv2;
	int i;

	gettimeofday( &tv1, NULL );
	for( i = 0; i < 1000000; i++ ){
		gettimeofday( &tv2, NULL );
	}
	printf( "1000000 gettimeofday takes: %dus\n", ( tv2.tv_sec - tv1.tv_sec ) 
		* 1000000 + tv2.tv_usec - tv1.tv_usec );
}
