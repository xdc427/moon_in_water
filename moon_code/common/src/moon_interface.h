#ifndef _MOON_INTERFACE_H_
#define _MOON_INTERFACE_H_
#include"moon_common.h"
#include<string.h>
#include<stdlib.h>

typedef struct inter_head_s{
	int len;
}__attribute__((aligned(sizeof(long)))) inter_head_s;
typedef inter_head_s * inter_head;

typedef struct inter_bottom_s{
	int start_offset;
	int num;
	const struct inter_bottom_s * next;
}__attribute__((aligned(sizeof(long)))) inter_bottom_s;
typedef inter_bottom_s * inter_bottom;

typedef struct inter_elem_s{
	int offset;
	int name_len;
	const char * name;
}__attribute__((aligned((sizeof(long))))) inter_elem_s;
typedef inter_elem_s * inter_elem;

#define _CONNECT_STRING( a, b ) a##b
#define CONNECT_STRING( a, b ) _CONNECT_STRING( a, b )
#define _TOSTRING( a ) #a
#define TOSTRING( a ) _TOSTRING( a )


#define INTERFACE_NAME( name ) CONNECT_STRING( name, _name )
#define _DECLARE_START( name ) static const char INTERFACE_NAME( name )[]__attribute__((aligned(sizeof(long))))
#define _DECLARE_END( name ) name##123456789abcdef
#define DECLARE_INTERFACE( name ) _DECLARE_START( name ) = TOSTRING( _DECLARE_END( name ) )

#define COMPARE_LEN( name ) ( sizeof( long ) <= 16 ?\
 ( STRLEN( INTERFACE_NAME( name ) ) / sizeof( long ) - 15 / sizeof( long ) ) * sizeof( long ) :\
 STRLEN( INTERFACE_NAME( name ) ) - 15 )

#define CACULATE_INTERFACE_ENTITY_LEN( interfaces_len, interface_num ) \
 ( sizeof( inter_head_s ) + ( interfaces_len ) + ( interface_num ) * sizeof( inter_elem_s ) + sizeof( inter_bottom_s ) )

#define INIT_INTERFACE_ENTITY( p_i_start, p_i_end ) \
do{\
	inter_bottom _p_i_bottom;\
	\
	( ( inter_head )( p_i_start ) )->len = POINT_OFFSET( p_i_end, p_i_start );\
	_p_i_bottom = ( inter_bottom )( p_i_end ) - 1;\
	_p_i_bottom->start_offset = POINT_OFFSET( _p_i_bottom, ( p_i_start ) );\
}while( 0 )

#define MALLOC_INTERFACE_ENTITY( len, interfaces_len, interface_num ) ({\
	void * _p_ret = NULL;\
	int _i_len;\
	char * _buf;\
	\
	_i_len = CACULATE_INTERFACE_ENTITY_LEN( interfaces_len, interface_num );\
	_buf = calloc( _i_len + ( len ), 1 );\
	if( _buf != NULL ){\
		_p_ret = _buf + _i_len;\
		INIT_INTERFACE_ENTITY( _buf, _p_ret );\
	}\
	_p_ret;\
})

#define GET_INTERFACE_START_POINT( p_data ) ({\
	inter_bottom _p_i_bottom;\
	\
	_p_i_bottom = ( inter_bottom )( p_data ) - 1;\
	( void * )( ( char * )_p_i_bottom - _p_i_bottom->start_offset );\
})

#define BEGAIN_INTERFACE( p_data ) do{\
	inter_bottom _p_i_bottom;\
	inter_elem _p_i_elem;\
	char * _p_i_hub;\
	\
	_p_i_bottom = ( inter_bottom )( p_data ) - 1;\
	_p_i_elem = ( inter_elem )_p_i_bottom - _p_i_bottom->num - 1;\
	_p_i_hub = ( ( char * )_p_i_bottom - _p_i_bottom->start_offset )

#define GET_INTERFACE( i_type ) ({\
	_p_i_elem->offset = POINT_OFFSET( _p_i_elem, _p_i_hub );\
	_p_i_elem->name = INTERFACE_NAME( i_type );\
	_p_i_elem->name_len = STRLEN( INTERFACE_NAME( i_type ) );\
	_p_i_elem--;\
	_p_i_bottom->num++;\
	_p_i_hub += sizeof( i_type );\
	( i_type * )( _p_i_hub - sizeof( i_type ) );\
})

#define END_INTERFACE( i_next ) _p_i_bottom->next = ( i_next ); }while( 0 )

#define I_MEM_NAME( i_type ) CONNECT_STRING( i_type, _entity )
#define STATIC_BEGAIN_GLOBAL_INTERFACE( var_name ) struct CONNECT_STRING( var_name, _i_s ){\
	inter_head_s i_head;
#define STATIC_BEGAIN_INTERFACE( var_name ) static struct{\
	inter_head_s i_head;
#define STATIC_DECLARE_INTERFACE( i_type ) i_type I_MEM_NAME( i_type );
#define STATIC_END_DECLARE_INTERFACE( var_name, interface_num, ... ) \
	inter_elem_s i_elems[ interface_num ];\
	inter_bottom_s i_bottom;\
	__VA_ARGS__;\
}__attribute__((packed)) var_name = {\
.i_head = { .len = sizeof( var_name ) },\
.i_bottom = { .start_offset = POINT_OFFSET( &var_name.i_bottom, &var_name ),\
.num = interface_num }

#define STATIC_GET_INTERFACE( var_name, i_type, i_index ) \
, .i_elems[ i_index ] = {\
	.offset = POINT_OFFSET( &var_name.i_elems[ i_index ], &var_name.I_MEM_NAME( i_type ) ),\
	.name = INTERFACE_NAME( i_type ),\
	.name_len = STRLEN( INTERFACE_NAME( i_type ) )\
}, .I_MEM_NAME( i_type )
#define STATIC_INIT_USERDATA( name ) , .name 
#define STATIC_END_INTERFACE( i_next ) , .i_bottom.next = i_next };
//put include file
#define EXPORT_GLOBAL_INTERFACE( var_name ) \
struct CONNECT_STRING( var_name, _i_s );\
extern struct CONNECT_STRING( var_name, _i_s ) var_name

#define GET_STATIC_INTERFACE_HANDLE( var_name ) ( ( void * )&var_name )

#define FIND_INTERFACE( p_data, i_type ) ({\
	const inter_bottom_s * _p_i_bottom;\
	inter_elem _p_i_elem, _p_i_elem_end;\
	i_type * _p_ret = NULL;\
	\
	_p_i_bottom = ( inter_bottom )( p_data );\
	do{\
	_p_i_bottom -= 1;\
	_p_i_elem = ( inter_elem )_p_i_bottom - 1;\
	_p_i_elem_end = _p_i_elem - _p_i_bottom->num;\
	for( ; _p_i_elem > _p_i_elem_end && ( _p_i_elem->name_len != STRLEN( INTERFACE_NAME( i_type ) )\
			|| memcmp( _p_i_elem->name, INTERFACE_NAME( i_type ), COMPARE_LEN( i_type ) ) != 0 )\
			; _p_i_elem-- )\
		;\
	if( _p_i_elem > _p_i_elem_end ){\
		_p_ret = ( i_type * )( ( char * )_p_i_elem - _p_i_elem->offset );\
		break;\
	}\
	if( ( _p_i_bottom = _p_i_bottom->next ) == NULL ){\
		break;\
	}\
	_p_i_bottom = ( inter_bottom )( ( char * )_p_i_bottom + ( ( inter_head )_p_i_bottom )->len );\
	}while( 1 );\
	_p_ret;\
})

#define CALL_INTERFACE_HANDLE_FUNC( p_inter, func, ... ) do{\
	typeof( p_inter ) _p_inter = p_inter;\
	if( _p_inter != NULL ){\
		CALL_FUNC( _p_inter->func, ##__VA_ARGS__ );\
	}\
}while( 0 )

#define CALL_INTERFACE_FUNC( p_data, i_type, i_func, ... ) CALL_INTERFACE_HANDLE_FUNC( FIND_INTERFACE( p_data, i_type ), i_func, p_data, ##__VA_ARGS__ )

#endif

