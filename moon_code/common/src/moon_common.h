#ifndef _MOON_COMMON_H_
#define _MOON_COMMON_H_
#include<stdint.h>
#include<string.h>
#include<stdio.h>
#include<stdlib.h>
#include"moon_debug.h"

#ifdef MODENAME
#undef MODENAME
#endif
#define MODENAME "moon_common"

#define IF_FREE( p ) do{\
	if( ( p ) != NULL ){\
		free( ( p ) );\
	}\
}while( 0 )

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
		_left = ( bit_len ) - _i * LONG_BITS;\
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

#define ARRAY_LEN( array ) ( sizeof( array ) / sizeof( ( array )[ 0 ]) )
#define POINT_OFFSET( ptr1, ptr2 ) ( ( char * )( ptr1 ) - ( char * )( ptr2 ) )
#define STRLEN( str ) ( sizeof( str ) - 1 )
#ifdef LEVEL_TEST
#define is_little() 1
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

#define CALL_FUNC( func, ... ) do{\
	typeof( func ) _func = func;\
	if( _func != NULL ){\
		_func( __VA_ARGS__ );\
	}\
}while( 0 )

#define CALL_RETURN_FUNC( func, ret, ... ) ({\
	typeof( func ) _func = func;\
	if( _func != NULL ){\
		ret = _func( __VA_ARGS__ );\
	}\
	ret;\
})

#define CUT_STRING( str, index, c_tmp ) do{\
	c_tmp = ( str )[ index ];\
	( str )[ index ] = '\0';\
}while( 0 )

#define RECOVER_STRING( str, index, c_tmp ) do{\
	( str )[ index ] = c_tmp;\
}while( 0 )

#define MIN( a, b ) ( ( a ) > ( b ) ? ( b ) : ( a ) )
#define MAX( a, b ) ( ( a ) < ( b ) ? ( b ) : ( a ) )

#ifdef MOON_TEST
#undef USEING_XID
#define USEING_XID "common_useing"
#endif
//专用于垃圾收集的
//状态与计数结合的深度为2的引用
enum{
	STATUS_CLOSED = 0x0f,
	STATUS_MASK = 0x0f, //低四位为状态
	USEING_REF_UNIT = 0x10
};
typedef void ( * closed_when_useing_ref0 )( void * ptr );

#define STATUS_LEGAL_CLOSED( status ) ( status == STATUS_CLOSED )
static inline int useing_ref_inc( int * p_status )
{
	int tmp;

	do{
		tmp = *p_status;
	}while( ( ( tmp & STATUS_MASK ) != STATUS_CLOSED ) 
			&& !__sync_bool_compare_and_swap( p_status, tmp, tmp + USEING_REF_UNIT ) );
	if( ( tmp & STATUS_MASK ) == STATUS_CLOSED ){
		return -1;
	}
	if( tmp + USEING_REF_UNIT < 0 ){
		MOON_PRINT_MAN( ERROR, "useing ref up overflow!" );
	}
	MOON_PRINT( TEST, USEING_XID, "%p:useing:1", p_status );
	return 0;
}

static inline int useing_ref_dec( int * p_status, closed_when_useing_ref0 close_fun, void * p_data )
{
	int tmp;
	
	MOON_PRINT( TEST, USEING_XID, "%p:useing:-1", p_status );
	tmp = __sync_sub_and_fetch( p_status, USEING_REF_UNIT );
	if( tmp < 0 ){
		MOON_PRINT_MAN( ERROR, "useing ref dowm overflow" );
	}else if( tmp == STATUS_CLOSED ){
		MOON_PRINT( TEST, USEING_XID, "%p:closed:1", p_status );
		close_fun( p_data );
	}
	return 0;
}

static inline int set_status_closed( int * p_status, closed_when_useing_ref0 close_fun, void * p_data )
{
	int tmp;

	do{
		tmp = *p_status;
	}while( ( tmp & STATUS_MASK ) != STATUS_CLOSED 
			&& !__sync_bool_compare_and_swap( p_status, tmp, ( tmp & ~STATUS_MASK ) | STATUS_CLOSED ) );
	if( ( tmp & STATUS_MASK ) != STATUS_CLOSED && tmp < USEING_REF_UNIT ){
		MOON_PRINT( TEST, USEING_XID, "%p:closed:1", p_status );
		close_fun( p_data );
	}
	return 0;
}
#ifdef MOON_TEST
#undef USEING_XID
#endif

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
	int balance; //0为平衡，-1左边比右边大1，1右边比左边大1
};
typedef struct avl_tree_s avl_tree_s;
typedef struct avl_tree_s * avl_tree;

struct mem_block_s;
struct addr_pair_s{
	union{
		unsigned long long virtual_addr;
		unsigned long long id;
	};
	union{
		struct mem_block_s * addr;
		unsigned long num_data;
		void * ptr;
	};
};
typedef struct addr_pair_s addr_pair_s;
typedef struct addr_pair_s * addr_pair;
typedef struct addr_pair_s id_pair_s;
typedef struct addr_pair_s * id_pair;

typedef union{
	void * ptr;
	int i_num;
	unsigned long long ull_num;
	double d_num;
} common_user_data_u;
typedef common_user_data_u * common_user_data;

//ret: 0 continue; <0 error quit; >0 normal quit
typedef int( *traver_func )( void *, common_user_data_u );
#define HASH_NUM 67
#define HASH_BASE 61

typedef struct hash_table_s{
	double_list_s table[ HASH_NUM ];
	int num;
} hash_table_s;
typedef hash_table_s * hash_table;

struct hash_key_s{
	char key[256];
};
typedef struct hash_key_s hash_key_s;
typedef struct hash_key_s * hash_key;

//dlist
static inline double_list data_to_list( void * p_data )
{
	return p_data == NULL? NULL: ( double_list )p_data - 1;
}

static inline void * list_to_data( double_list p_dlist )
{
	return p_dlist == NULL? NULL: ( void * )( p_dlist + 1 );
}

static inline void * dlist_malloc( int len )
{
	double_list p_dlist;
	
	p_dlist = calloc( sizeof( double_list_s ) + len, 1 );
	return list_to_data( p_dlist );
}

static inline void dlist_free( void * p_data )
{
	IF_FREE( data_to_list( p_data ) );
}

//返回插入后的data指针
static inline void * dlist_insert( void * p_data_head, void * p_data )
{
	double_list p_dlist, p_dlist_next;
	
	if( p_data_head == NULL ){
		return p_data;
	}
	p_dlist_next = data_to_list( p_data_head );
	p_dlist = data_to_list( p_data );
	p_dlist->next = p_dlist_next;
	p_dlist->prev = p_dlist_next->prev;
	p_dlist_next->prev = p_dlist;
	if( p_dlist->prev != NULL ){
		p_dlist->prev->next = p_dlist;
	}
	return p_data;
}

//返回插入后的data指针
static inline void * dlist_append( void * p_data_head, void * p_data )
{
	double_list p_dlist, p_dlist_prev;

	if( p_data_head == NULL ){
		return p_data;
	}
	p_dlist_prev = data_to_list( p_data_head );
	p_dlist = data_to_list( p_data );
	p_dlist->prev = p_dlist_prev;
	p_dlist->next = p_dlist_prev->next;
	p_dlist_prev->next = p_dlist;
	if( p_dlist->next != NULL ){
		p_dlist->next->prev = p_dlist;
	}
	return p_data;
}

//返回删除元素后面的data指针
static inline void * dlist_del( void * p_data )
{
	double_list p_dlist;

	p_dlist = data_to_list( p_data );
	if( p_dlist->prev != NULL ){
		p_dlist->prev->next = p_dlist->next;
		p_dlist->prev = NULL;
	}
	if( p_dlist->next != NULL ){
		p_dlist->next->prev = p_dlist->prev;
		p_dlist->next = NULL;
	}
	return list_to_data( p_dlist->next );
}

//切断p_data与前面元素的链接，返回p_data
static inline void * dlist_cut( void * p_data )
{
	double_list p_dlist;

	p_dlist = data_to_list( p_data );
	if( p_dlist->prev != NULL ){
		p_dlist->prev->next = NULL;
		p_dlist->prev = NULL;
	}
	return p_data;
}

static inline void * dlist_prev( void * p_data )
{
	return list_to_data( data_to_list( p_data )->prev );
}

static inline void * dlist_next( void * p_data )
{
	return list_to_data( data_to_list( p_data )->next );
}

//这种非命名型删除，需要判断
static inline int is_really_del( void * data )
{
	if( dlist_prev( data ) == NULL && dlist_next( data ) == NULL ){
		return 0;
	}else{
		dlist_del( data );
		return 1;
	}
}

//hash
static inline unsigned long hash_func( const char * key )
{
	unsigned long hash;

	hash = 0;
	for( ; *key != '\0'; key++ ){
		hash *= HASH_BASE;
		hash += *key;
		hash %= HASH_NUM;
	}
	return hash;
}

static inline void hash_del( void * p_data )
{
	dlist_del( ( hash_key )p_data - 1 );
}

static inline int is_hash_really_del( void * p_data )
{
	return is_really_del( ( hash_key )p_data - 1 );
}

static inline void hash_table_del( hash_table p_hash_table, void * p_data )
{
	p_hash_table->num--;
	hash_del( p_data );
}

static inline int is_hash_table_really_del( hash_table p_hash_table, void * p_data )
{
	int ret;

	if( ( ret = is_really_del( p_data ) ) != 0 ){
		p_hash_table->num--;
	}
	return ret;
}

static inline void hash_free( void * p_data )
{
	dlist_free( ( hash_key )p_data - 1 );
}

static inline void * hash_malloc( int data_len )
{
	hash_key p_key;

	p_key = dlist_malloc( sizeof( *p_key ) + data_len );
	if( p_key != NULL ){
		return p_key + 1;
	}
	return NULL;
}

static inline void * hash_search2( double_list table, char * key, void * p_data )
{
	hash_key p_key;
	unsigned long index;

	index = hash_func( key );
	p_key = list_to_data( table[ index ].next );
	for( ; p_key != NULL; p_key = dlist_next( p_key ) ){
		if( strcmp( p_key->key, key ) == 0 ){
			return p_key + 1;
		}
	}
	if( p_data != NULL ){ //new
		p_key = ( hash_key )p_data - 1; 
		snprintf( p_key->key, sizeof( p_key->key ), "%s", key );
		dlist_append( list_to_data( &table[ index ] ), p_key );
		return p_key + 1;
	}
	return NULL;
}

static inline void * hash_search( double_list table, char * key, int data_len )
{
	void * p_ret;
	void * p_data = NULL;

	if( data_len > 0 ){
		p_data = hash_malloc( data_len );
	}
	p_ret = hash_search2( table, key, p_data );
	if( p_ret != p_data && p_data != NULL ){
		hash_free( p_data );
	}
	return p_ret;
}

static inline void * hash_table_search2( hash_table p_hash_table, char * key, void * p_data )
{
	void * ptr;
	
	ptr = hash_search2( p_hash_table->table, key, p_data );
	if( ptr != NULL && ptr == p_data ){
		p_hash_table->num++;
	}
	return ptr;
}

static inline void * hash_table_search( hash_table p_hash_table, char * key, int data_len )
{
	void * p_ret;
	void * p_data = NULL;

	if( data_len > 0 ){
		p_data = hash_malloc( data_len );
	}
	p_ret = hash_table_search2( p_hash_table, key, p_data );
	if( p_ret != p_data && p_data != NULL ){
		hash_free( p_data );
	}
	return p_ret;
}

static inline int hash_traver( double_list table, traver_func func, common_user_data_u user_data )
{
	hash_key p_key;
	int i, ret;

	for( i = 0; i < HASH_NUM; i++ ){
		p_key = list_to_data( table[ i ].next );
		for( ; p_key != NULL; p_key = dlist_next( p_key ) ){
			if( ( ret = func( p_key + 1, user_data ) ) != 0 ){
				return ret;
			}
		}
	}
	return 0;
}

static inline int hash_table_traver( hash_table p_hash_table, traver_func func, common_user_data_u user_data )
{
	return hash_traver( p_hash_table->table, func, user_data );
}

static inline void * hsah_insert( void * p_data, void * p_insert )
{
	return dlist_insert( ( hash_key )p_data - 1, ( hash_key )p_insert - 1 );
}

static inline void * hash_append( void * p_data, void * p_append )
{
	return dlist_append( ( hash_key )p_data - 1, ( hash_key )p_append - 1 );
}

static inline int get_hash_table_num( hash_table p_hash_table )
{
	return p_hash_table->num;
}

static inline void get_key_of_value( void * p_data, char * key, int len )
{
	snprintf( key, len, "%s", ( ( hash_key )p_data - 1 )->key );
}

static inline void set_key_of_value( void * p_data, char * key )
{
	hash_key p_key;

	p_key = ( hash_key )p_data -1;
	snprintf( p_key->key, sizeof( p_key->key ), "%s", key );
}

//list
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

static inline void print_binary( unsigned char * b_array, int len )
{
	int i;

	for( i = 0; i < len; i++ ){
		printf( "%02X", b_array[ i ] );
	}
}

static inline void sprint_binary( char * buf, unsigned char * b_array, int len )
{
	int i;

	for( i = 0; i < len; i++ ){
		sprintf( buf + 2 * i, "%02X", b_array[ i ] );
	}
}

//avl interface
int avl_traver_first( avl_tree avl, int ( *func )( addr_pair, void * ), void * para );
void avl_print( avl_tree avl );
void avl_free( avl_tree * pavl );
addr_pair avl_search( avl_tree avl, unsigned long long addr );
addr_pair avl_add( avl_tree * pavl, unsigned long long addr );
addr_pair_s avl_del( avl_tree * pavl, unsigned long long addr );
addr_pair avl_leftest_node( avl_tree avl );

//max_min heap
typedef struct max_min_heap_s * max_min_heap;
max_min_heap heap_init( int is_min, int elem_size, int capacity, int ( * compare )( void *, void * ) );
void heap_free( max_min_heap p_heap );
int heap_push( max_min_heap p_heap, void * p_data );
int hash_pop( max_min_heap p_heap, void * p_data );
int heap_length( max_min_heap p_heap );
int heap_top( max_min_heap p_heap, void * p_data );

#endif

