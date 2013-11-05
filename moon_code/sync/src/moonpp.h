#ifndef _MOONPP_H_
#define _MOONPP_H_

#include"moon_runtime.h"

#define GET_MOON_ID( id ) _GET_MOON_ID( id )
#define _GET_MOON_ID( id ) #id

#define SHADOW_GET( p_data ) ({ \
	typeof ( p_data ) _p_tmp; \
	_p_tmp = shadow_runtime( GET_MOON_ID( MOON_ID ), ( p_data ), sizeof( *( p_data ) ), "r" ); \
	*( _p_tmp ); \
})

#define SHADOW_MEMGET( p_data, dst, len ) ({\
	typeof ( p_data ) _p_tmp; \
	_p_tmp = shadow_runtime( GET_MOON_ID( MOON_ID ), ( p_data ), ( len ), "r" ); \
	memcpy( ( dst ), _p_tmp, ( len ) ); \
	( len );\
})

#define SHADOW_SET( p_data, num ) ({\
	typeof ( p_data ) _p_tmp; \
	_p_tmp = shadow_runtime( GET_MOON_ID( MOON_ID ), ( p_data ), sizeof( *( p_data ) ), "w" ); \
	*( _p_tmp ) = ( num ); \
	0;\
})

#define SHADOW_MEMSET( p_data, src, len ) ({\
	typeof ( p_data ) _p_tmp; \
	_p_tmp = shadow_runtime( GET_MOON_ID( MOON_ID ), ( p_data ), ( len ), "w" ); \
	memcpy( _p_tmp, ( src ), ( len ) ); \
	( len );\
})

#define SHADOW_ADDRESS( p_data ) ({ \
	( p_data ); \
})

//有可能为shadow_point address
#define SHADOW_POINT_CMP( p_data, opt, cmp ) ({ \
	typeof ( p_data ) _p_tmp; \
	_p_tmp = shadow_runtime( GET_MOON_ID( MOON_ID ), ( p_data ), sizeof( *( p_data ) ), "c" ); \
	( *( _p_tmp ) opt ( cmp ) ); \
})

#define SHADOW_CONNNECT( host )
#define SHADOW_SYNC()
#define SHADOW_VERSION( ver )
#define SHADOW_EVENT( handler )

#define SHADOW_VAR 
#define SHADOW_POINT 
#define SHADOW_STRUCT 
#define SHADOW_HIDE
#define SHADOW_INCLUDE
#define SHADOW_NEW( type, n ) shadow_new( GET_MOON_ID( MOON_ID ), #type, n, sizeof( type ) * ( n ) )
#define SHADOW_DEL( shadow_point ) shadow_del( GET_MOON_ID( MOON_ID ), shadow_point )

#endif

