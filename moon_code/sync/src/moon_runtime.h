#ifndef _MOON_RUNTIME_H_
#define _MOON_RUNTIME_H_

void * shadow_runtime( char * type, void * p_data, int len, char * opt );
void * shadow_new( char * type, char * struct_type, int num, int size );
void   shadow_del( char * type, void * addr );
#ifdef LEVEL_TEST
int test_moon_num( );
#endif
enum{
	MOON_UPDATE = 0x1
};

#define NEWEST_VERSION ( ~0 )

#endif
