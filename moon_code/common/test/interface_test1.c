#include<stdio.h>
#include<stdlib.h>
#include"interface_test.h"
#include"moon_interface.h"

CREATE_FUNC( a_func, a_interface_s )
CREATE_FUNC( b_func1, b_interface_s )
CREATE_FUNC( b_func2, b_interface_s )
CREATE_FUNC( c_func1, c_interface_s )
CREATE_FUNC( c_func2, c_interface_s )
CREATE_FUNC( c_func3, c_interface_s )


STATIC_BEGAIN_GLOBAL_INTERFACE( test_c )
STATIC_DECLARE_INTERFACE( c_interface_s )
STATIC_END_DECLARE_INTERFACE( test_c, 1 )
STATIC_GET_INTERFACE( test_c, c_interface_s, 0 ) = {
.func1 = c_func1,
.func2 = c_func2,
.func3 = c_func3
}
STATIC_END_INTERFACE( NULL )


