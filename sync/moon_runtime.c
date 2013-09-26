#include<stdio.h>
#include<stdlib.h>

void * shadow_runtime( void * p_data, int len, char *opt )
{
	printf( "%p:%s\n", p_data, opt );
	return p_data;
}

void *shadow_new( char * type, int num )
{
	printf( "shadow_new:%d\n", num );
	return malloc( num );
}

void shadow_del( void *addr )
{
	printf( "shadow_del:%p\n", addr );
	free( addr );
}	
