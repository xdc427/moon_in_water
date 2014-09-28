#include<stdio.h>
#include<inttypes.h>
#include"moon_common.h"

#define TEST_CONVERT( num, type ) {\
	type _a, _b;\
	\
	_a = _b = num;\
	convert_##type( &_b );\
	printf( "convert " #type ":( 0x%llX:0x%llX )\n", ( unsigned long long )_a, ( unsigned long long )_b );\
	convert_##type( &_b );\
	if( _a != _b ){\
		printf( "convert " #type "error!\n" );\
	}\
}

int main()
{
	unsigned align = sizeof( long );
	uint64_t n, m;

	if( is_little() ){
		printf( "cur machine is Little Endian\n");
	}else{
		printf( "cur machine is Big Endian\n");
	}

	TEST_CONVERT( 0xA0B0, uint16_t );
	TEST_CONVERT( 0xA0B0C0D0, uint32_t );
	TEST_CONVERT( 0x8090A0B0C0D0E0F0, uint64_t );

	n = 0xfffffffffffffff2;
	m = ROUND_UP( n, align );
	printf( "0x%"PRIx64" round_up 0x%"PRIx64"\n", n, m );
	return 0;
}

