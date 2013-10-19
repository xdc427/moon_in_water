#ifndef _MOON_RUNTIME_H_
#define _MOON_RUNTIME_H_

void * shadow_runtime( void * p_data, int len, char * opt );
void * shadow_new( char * type, int num );
void   shadow_del( void * addr );

#endif
