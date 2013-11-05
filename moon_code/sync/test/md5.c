#include<openssl/md5.h>
#include<stdio.h>
#include<string.h>

void main( int argc, char * argv[] )
{
	unsigned char md5_sun[ 16 ];
	int i;

	if( argc < 2 ){
		return;
	}
	MD5( argv[ 1 ], strlen( argv[ 1 ] ), md5_sun );
	for( i = 0; i < sizeof( md5_sun ); i++ ){
		printf( "%02x", md5_sun[ i ]);
	}
	printf( "\n" );
}
