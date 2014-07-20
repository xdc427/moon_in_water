#ifndef _MOON_RUNTIME_H_
#define _MOON_RUNTIME_H_

void * shadow_runtime( char * type, void * p_data, int len, char * opt );
void * shadow_new( char * type, char * struct_type, int num, int size );
void   shadow_del( char * type, void * addr );
void   version_init( );
int shadow_entity_add( char * type, char * xid, int is_shadow );
int shadow_commit( );
void shadow_env_set( void * tp );
void * shadow_env_new( char *type, char * xid, unsigned long ver_num );
#ifdef LEVEL_TEST
int test_moon_num( );
void shadow_print_types( char * type );
void shadow_print_runtime( );
void avl_test();
#endif
enum{
	MOON_UPDATE = 0x1
};

#define NEWEST_VERSION ( ~0 )

#endif
