#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#define BUFSIZE ( 1024 * 1024 )

int main( int argc, char * argv[] )
{
	char *buf1, *buf2;
	FILE * fp1, *fp2; 
	int ret1, ret2;
	int ret, total;

	if( argc < 3 ){
		return -1;
	}
	fp1 = fopen( argv[ 1 ], "r" );
	fp2 = fopen( argv[ 2 ], "r" );
	buf1 = malloc( BUFSIZE );
	buf2 = malloc( BUFSIZE );
	if( fp1 == NULL || fp2 == NULL || buf1 == NULL || buf2 == NULL ){
		if( fp1 != NULL )fclose( fp1 );
		if( fp2 != NULL )fclose( fp2 );
		printf( "init error!\n" );
		return -1;
	}
	total = ret = 0;
	while( 1 ){
		ret1 = fread( buf1, 1, BUFSIZE, fp1 );
		ret2 = fread( buf2, 1, BUFSIZE, fp2 );
		if( ret1 == ret2 ){
			total += ret1;
			if( ret1 == 0 )break;
			if( memcmp( buf1, buf2, ret ) != 0 ){
				ret = -1;
				printf( "compare error\n" );
				break;
			}
		}else{
			printf( "read num:%d:%d\n", ret1, ret2 );
			ret = -1;
			break;
		}
	}
	printf( "%s %s %d\n", argv[ 1 ], argv[ 2 ], total );
	fclose( fp1 );
	fclose( fp2 );
	return ret;
}

