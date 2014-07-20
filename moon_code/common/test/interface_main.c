#include<stdio.h>
#include"moon_interface.h"
#include"interface_test.h"

typedef struct{
	int a;
	char b;
}__attribute__((aligned((sizeof(long))))) align_s;
typedef align_s * align;


CREATE_FUNC( a_func, a_interface_s )
CREATE_FUNC( b_func1, b_interface_s )
CREATE_FUNC( b_func2, b_interface_s )
CREATE_FUNC( c_func1, c_interface_s )
CREATE_FUNC( c_func2, c_interface_s )
CREATE_FUNC( c_func3, c_interface_s )


STATIC_BEGAIN_INTERFACE( main_interb_entity )
STATIC_DECLARE_INTERFACE( a_interface_s )
STATIC_DECLARE_INTERFACE( b_interface_s )
STATIC_END_DECLARE_INTERFACE( main_interb_entity, 1 )
STATIC_GET_INTERFACE( main_interb_entity, b_interface_s, 0 ) = {
	.func1 = b_func1,
	.func2 = b_func2
}
STATIC_END_INTERFACE( GET_STATIC_INTERFACE_HANDLE( test_c ) )


STATIC_BEGAIN_INTERFACE( main_inter_entity )
STATIC_DECLARE_INTERFACE( a_interface_s )
STATIC_END_DECLARE_INTERFACE( main_inter_entity, 1 )
STATIC_GET_INTERFACE( main_inter_entity, a_interface_s, 0 ) = {
	.func = a_func
}
STATIC_END_INTERFACE( GET_STATIC_INTERFACE_HANDLE( main_interb_entity ) )

void main()
{
	align p_test, p_testb;
	a_interface_s * p_a_inter;
	c_interface_s * p_c_inter;

	printf( "%d:%d\n", ( int )sizeof( main_inter_entity ), ( int )sizeof( main_interb_entity ) );
	p_test = ( align )MALLOC_INTERFACE_ENTITY( sizeof( *p_test ), 0, 0 );
	BEGAIN_INTERFACE( p_test );
	END_INTERFACE( GET_STATIC_INTERFACE_HANDLE( main_inter_entity ) );
	p_test->a = 10;
	FIND_INTERFACE( p_test, a_interface_s )->func( p_test->a );
	FIND_INTERFACE( p_test, b_interface_s )->func2( p_test->a );
	printf( "%p\n", FIND_INTERFACE( p_test, c_interface_s ) );
	FIND_INTERFACE( p_test, c_interface_s )->func3( p_test->a );

	p_testb = ( align )MALLOC_INTERFACE_ENTITY( sizeof( *p_testb ), sizeof( a_interface_s ) + sizeof( c_interface_s ), 2 );
	BEGAIN_INTERFACE( p_testb );
	p_a_inter = GET_INTERFACE( a_interface_s );
	p_a_inter->func = a_func;
	p_c_inter = GET_INTERFACE( c_interface_s );
	p_c_inter->func1 = c_func1;
	p_c_inter->func2 = c_func2;
	p_c_inter->func3 = c_func3;
	END_INTERFACE( GET_STATIC_INTERFACE_HANDLE( main_interb_entity ) );
	p_testb->b = 10;
	FIND_INTERFACE( p_testb, b_interface_s )->func1( p_testb->b );
	FIND_INTERFACE( p_testb, b_interface_s )->func2( p_testb->b );
	printf( "%p:%p\n", FIND_INTERFACE( p_testb, a_interface_s ), FIND_INTERFACE( p_testb, c_interface_s ) );
	FIND_INTERFACE( p_testb, c_interface_s )->func1( p_testb->b );
	FIND_INTERFACE( p_testb, c_interface_s )->func2( p_testb->b );
	FIND_INTERFACE( p_testb, c_interface_s )->func3( p_testb->b );

	CALL_INTERFACE_FUNC( p_testb, c_interface_s, func1 );
}

