#include<stdio.h>
#include"moon_common.h"

#define TEST_CONVERT( num, type ) {\
	type _a, _b;\
	\
	_a = _b = num;\
	convert_##type( &_b );\
	printf( "convert " #type ":( 0x%llX:0x%llX )\n", ( uint64_t )_a, ( uint64_t )_b );\
	convert_##type( &_b );\
	if( _a != _b ){\
		printf( "convert " #type "error!\n" );\
	}\
}

void main()
{
	uint16_t u16, u16_t;
	uint32_t u32, u32_t;
	uint64_t u64, u64_t;

	if( is_little() ){
		printf( "cur machine is Little Endian\n");
	}else{
		printf( "cur machine is Big Endian\n");
	}

	TEST_CONVERT( 0xA0B0, uint16_t );
	TEST_CONVERT( 0xA0B0C0D0, uint32_t );
	TEST_CONVERT( 0x8090A0B0C0D0E0F0, uint64_t );
}

