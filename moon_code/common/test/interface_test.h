#ifndef _INTERA_H_
#define _INTERA_H_

#include"moon_interface.h"

#define CREATE_FUNC( func_name, inter_name ) \
static void func_name( int n )\
{\
	printf( __FILE__":"TOSTRING( inter_name )":"TOSTRING( func_name )":%d\n", n );\
}

typedef struct a_interface_s{
	void ( * func )( int );
} a_interface_s;
DECLARE_INTERFACE( a_interface_s );

typedef struct b_interface_s{
	void ( *func1 )( int );
	void ( *func2 )( int );
} b_interface_s;
DECLARE_INTERFACE( b_interface_s );

typedef struct c_interface_s{
	void ( *func1 )( int );
	void ( *func2 )( int );
	void ( *func3 )( int );
} c_interface_s;
DECLARE_INTERFACE( c_interface_s );

EXPORT_GLOBAL_INTERFACE( test_c );

#endif
