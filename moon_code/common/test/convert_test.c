#include<stdio.h>
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

void main()
{
	if( is_little() ){
		printf( "cur machine is Little Endian\n");
	}else{
		printf( "cur machine is Big Endian\n");
	}

	TEST_CONVERT( 0xA0B0, uint16_t );
	TEST_CONVERT( 0xA0B0C0D0, uint32_t );
	TEST_CONVERT( 0x8090A0B0C0D0E0F0, uint64_t );
}

