#ifndef _MOON_COMMON_H_
#define _MOON_COMMON_H_
#include<stdint.h>


struct list_s{
	struct list_s * next;
};
typedef struct list_s list_s;
typedef struct list_s * list;

struct double_list_s{
	struct double_list_s * prev;
	struct double_list_s * next;
};
typedef struct double_list_s double_list_s;
typedef struct double_list_s * double_list;

struct avl_tree_s{
	struct avl_tree_s * left;
	struct avl_tree_s * right;
	struct avl_tree_s * parent;
	long balance; //0为平衡，-1左边比右边大1，1右边比左边大1
};
typedef struct avl_tree_s avl_tree_s;
typedef struct avl_tree_s * avl_tree;

static inline void list_add_head( list * pp_head, list p_new )
{
	p_new->next = *pp_head;
	*pp_head = p_new;
}

static inline void list_add_last( list * pp_head, list p_new )
{
	for( ; *pp_head != NULL; pp_head = &( *pp_head )->next ){
		;
	}
	*pp_head = p_new;
}

#define LONG_BITS ( sizeof( long ) << 3 )
#define MAP_LEN( n ) ( ( n ) / LONG_BITS + 1 )//最多多一个long长度
#define SET_BIT( map, n ) do{\
	( map )[ ( n ) / LONG_BITS ] |= 1 << ( ( n ) % LONG_BITS ); \
}while( 0 )
//宏中定义的变量都下划线开头
#define BIT_MAP_TRAVER( map, bit_len, IS_SET_ACTION, IS_CLEAR_ACTION ) do{\
	int _i, _j, _len, _left;\
\
	_len = MAP_LEN( ( bit_len ) );\
	for( _i = 0; _i < _len; _i++ ){\
		_left = ( bit_len ) - _len * LONG_BITS;\
		_left = _left > LONG_BITS ? LONG_BITS : _left;\
		for( _j = 0; _j < _left; _j++ ){\
			if( ( ( map )[ _i ] & ( 1 << _j ) ) > 0 ){\
				IS_SET_ACTION( ( _i * LONG_BITS + _j ) );\
			}else{\
				IS_CLEAR_ACTION( ( _i * LONG_BITS ) + _j );\
			}\
		}\
	}\
}while( 0 )

#define LIST_TRAVER( p_list, type, func ) ({\
	int _ret;\
	list _p_tmp;\
\
	_ret = 0;\
	_p_tmp = ( p_list );\
	while( ( _p_tmp ) != NULL ){\
		_ret = func( ( ( type )( _p_tmp + 1 ) ) );\
		if( _ret != 0 ){\
			break;\
		}\
		_p_tmp = _p_tmp->next;\
	}\
	_ret;\
})

#define ARRAY_TRAVER( p_array, len, func  ) ({\
	int _ret, _i;\
\
	_ret = 0;\
	for( _i = 0; _i < ( len ); _i++ ){\
		_ret = func( ( &( p_array )[ _i ] ) );\
		if( _ret != 0 ){\
			break;\
		}\
	}\
	_ret;\
})
 
#ifdef LEVEL_TEST
#define is_little() 0
#else
static inline int is_little( )
{
	unsigned long i;
	unsigned char *p_c;
	
	i = 0xaa;
	p_c = ( unsigned char *)&i;
	return *p_c == i;
}
#endif

#define _CONVERT_NUM( pc, cur_type, times ) do{\
	int _i;\
	cur_type * _p;\
	int _half_bits = sizeof( cur_type ) << 2;\
\
	_p = ( cur_type * )( pc );\
	for( _i = 0; _i < ( times ); _i++ ){\
		_p[_i] = ( _p[_i] >> _half_bits ) | ( _p[_i] << _half_bits );\
	}\
}while( 0 )

static inline void convert_uint16_t( uint16_t * p_16 )
{
	if( !is_little() ){
		_CONVERT_NUM( p_16, uint16_t, 1 );
	}
}

static inline void convert_uint32_t( uint32_t * p_32 )
{
	if( !is_little() ){
		_CONVERT_NUM( p_32, uint16_t, 2 );
		_CONVERT_NUM( p_32, uint32_t, 1 );
	}
}
static inline void convert_uint64_t( uint64_t * p_64 )
{
	if( !is_little() ){
		_CONVERT_NUM( p_64, uint16_t, 4 );
		_CONVERT_NUM( p_64, uint32_t, 2 );
		_CONVERT_NUM( p_64, uint64_t, 1 );
	}
}

#endif
