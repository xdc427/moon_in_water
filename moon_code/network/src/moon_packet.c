#include"moon_debug.h"
#include<string.h>
#include<stdlib.h>
#include<stdio.h>
#include"moon_packet.h"
#include"moon_common.h"

struct packet_model_s{
	char id[ 64 ];
	int is_start;
	int direction;//-1 打包，1 解包
	int cur_index;
	int cur_position;
	int buf_len;
	char * buf;
	int elem_num;
	union{
		list elem_list;//list_s + packet_elem_s
		packet_elem elem_array;//在实例化时创建数组[ packet_elem_s ]
	};
};

static list model_head = NULL;//list_s + packet_model_s 目前为非线程安全

//json_str为json串，用来配制一个包的结构
//{ name:string_value, elem_set:[ { id:string_value, len:number_value, pack_func:number_value, unpack_func:number_value, free_func:number_value }] }
//现在暂且不用json

static inline void elem_list_print( list p_list )
{
#define PRINT_ELEM( p_elem ) ({\
	printf( "%s:%d->", ( p_elem )->id, ( p_elem )->len );\
	0;\
})
	LIST_TRAVER( p_list, packet_elem, PRINT_ELEM );
}

void packet_model_print( )
{
#define PRINT_MODEL( p_model ) ({\
	printf( "%s:%d\n", ( p_model )->id, ( p_model )->elem_num );\
	elem_list_print( ( p_model )->elem_list );\
	printf( "\n" );\
	0;\
})
	LIST_TRAVER( model_head, packet_model, PRINT_MODEL );
#undef PRINT_MODEL
}

void packet_instantiation_model_print( packet_model p_model )
{
	if( p_model == NULL ){
		return;
	}
	printf( "%s:%d\n", p_model->id, p_model->elem_num );
	ARRAY_TRAVER( p_model->elem_array, p_model->elem_num, PRINT_ELEM );
	printf( "\n" );
#undef PRINT_ELEM
}

static inline int has_model( packet_model p_model, void * data )
{
	if( strcmp( ( char * )data, p_model->id ) == 0 ){
		return 1;
	}
	return 0;
}

static inline void _packet_model_add( packet_model p_model, list p_new, int is_head )
{
	if( is_head ){
		list_add_head( &p_model->elem_list, p_new );
	}else{
		list_add_last( &p_model->elem_list, p_new );
	}
	p_model->elem_num++;
}

//name:packet_model的id
//p_elem:要插入的元素
//is_head: 0:插入尾部, !0:插入头部
//
//当不存在为name的model时，先创建一个而后插入
int packet_model_add( char * name, packet_elem p_elem, int is_head )
{
	list p_new, p_new2;
	packet_elem p_elem_new;
	packet_model p_model_new;
	int ret;

	if( name == NULL || p_elem == NULL || p_elem->len < 0 ){
		MOON_PRINT_MAN( ERROR, "input paramers error!" );
		return -1;
	}
	p_new = calloc( 1, sizeof( list_s ) + sizeof( packet_elem_s ) );
	if( p_new == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		return -1;
	}
	p_elem_new = ( packet_elem )( p_new + 1 );
	*p_elem_new = *p_elem;
#define FUNC( p_model ) ({\
	int _ret;\
\
	_ret = 0;\
	if( has_model( ( p_model ), name ) ){\
		_ret = 1;\
		_packet_model_add( p_model, p_new, is_head );\
	}\
	_ret;\
})
	ret = LIST_TRAVER( model_head, packet_model, FUNC );
#undef FUNC
	if( ret != 0 ){
		return 0;
	}
	p_new2 = calloc( 1, sizeof( list_s ) + sizeof( packet_model_s ) );
	if( p_new2 == NULL ){
		free( p_new );
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		return -1;
	}
	p_model_new = ( packet_model )( p_new2 + 1 );
	strncpy( p_model_new->id, name, sizeof( p_model_new->id ) - 1 );
	list_add_head( &model_head, p_new2 );
	_packet_model_add( p_model_new, p_new, is_head );
	return 0;
}

static inline packet_model _clone_packet_model( packet_model p_model )
{
	packet_model p_model_new;
	int i;

	p_model_new = NULL;
	if( p_model->elem_num <= 0 ){
		MOON_PRINT_MAN( ERROR, "now this model elem numbers is zero!" );
		return NULL;
	}
	p_model_new = calloc( 1, sizeof( packet_model_s ) 
			+ p_model->elem_num * ( sizeof( packet_elem_s ) ) );
	if( p_model_new == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		return NULL;
	}
	*p_model_new = *p_model;
	p_model_new->elem_array = ( packet_elem )( p_model_new + 1 );
	i = 0;
#define FUNC( p_elem ) ({\
	p_model_new->elem_array[ i ] = *( p_elem );\
	i++;\
	0;\
})
	LIST_TRAVER( p_model->elem_list, packet_elem, FUNC );
#undef FUNC
	return p_model_new;
}

static inline packet_model clone_packet_model( char * name )
{
	packet_model p_model_new = NULL;
	
#define FUNC( p_model ) ({\
	int _ret;\
\
	_ret = 0;\
	if( has_model( ( p_model ), name ) ){\
		_ret = 1;\
		p_model_new = _clone_packet_model( ( p_model ) );\
	}\
	_ret;\
})
	LIST_TRAVER( model_head, packet_model, FUNC );
#undef FUNC
	return p_model_new;
}

packet_model get_pack_instantiation( char * name )
{
	packet_model p_model_new;

	if( name == NULL ){
		MOON_PRINT_MAN( ERROR, "input parameter error!" );
		return NULL;
	}
	p_model_new = clone_packet_model( name );
	if( p_model_new == NULL ){
		return NULL;
	}
	p_model_new->direction = -1;
	p_model_new->cur_index = p_model_new->elem_num - 1;
	return p_model_new;
}

int create_packet_buf( packet_model p_model )
{
	int len, last;

	if( p_model == NULL || p_model->buf != NULL ){
		MOON_PRINT_MAN( ERROR, "input parameter error!" );
		return -1;
	}
	len = 0;
	last = 0;
#define FUNC( p_elem ) ({\
	len += ( p_elem )->len;\
	last = ( p_elem )->len;\
	0;\
})
	ARRAY_TRAVER( p_model->elem_array, p_model->elem_num, FUNC );
#undef FUNC
	if( len <= 0 ){
		MOON_PRINT_MAN( ERROR, "buffer length under zero!" );
		return -1;
	}
	p_model->buf = calloc( 1, len );
	if( p_model->buf == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		return -1;
	}
	p_model->buf_len = len;
	p_model->cur_position = len - last;
	return 0;
}

packet_model get_unpack_instantiation( char * name, char * buf, int len )
{
	packet_model p_model_new;

	if( name == NULL || buf == NULL || len <= 0 ){
		MOON_PRINT_MAN( ERROR, "input parameter error!" );
		return NULL;
	}
	p_model_new = clone_packet_model( name );
	if( p_model_new == NULL ){
		return NULL;
	}
	p_model_new->cur_index = 0;
	p_model_new->direction = 1;
	p_model_new->buf = buf;
	p_model_new->buf_len = len;
	return p_model_new;
}

//use in pack
int set_packet_elem_len( packet_model p_model, char * id, int len )
{
	int ret;

	if( p_model == NULL || id == NULL || len < 0 ){
		MOON_PRINT_MAN( ERROR, "intput parameter error!" );
		return -1;
	}
#define FUNC( p_elem ) ({\
	int _ret;\
\
	_ret = 0;\
	if( strcmp( ( p_elem )->id, id ) == 0 ){\
		_ret = 1;\
		( p_elem )->len = len;\
	}\
	_ret;\
})
	ret = ARRAY_TRAVER( p_model->elem_array, p_model->elem_num, FUNC );
#undef FUNC
	if( ret <= 0 ){
		MOON_PRINT_MAN( ERROR, "can't find id!" );
		return -1;
	}
	return 0;
}
//只有在解析时才可用带position的函数
//position >0 next, <0 prev, =0 cur

static inline int get_packet_position( packet_model p_model, int position )
{
	int next;

	if( position > 0 ){
		position = 1;
	}else if( position < 0 ){
		position = -1;
	}
	next = p_model->cur_index + position * p_model->direction;
	if( next < 0 || next >= p_model->elem_num ){
		MOON_PRINT_MAN( ERROR, "out of range!" );
		return -1;
	}
	return next;
}

int set_packet_elem_len_position( packet_model p_model, int len, int position )
{
	int next;

	if( p_model == NULL || len < 0 ){
		MOON_PRINT_MAN( ERROR, "intput parameter error!" );
		return -1;
	}
	next = get_packet_position( p_model, position );
	if( next < 0 ){
		return -1;
	}
	p_model->elem_array[ next ].len = len;
	return 0;
}

int get_packet_elem_len( packet_model p_model )
{
	int len;

	if( p_model == NULL ){
		MOON_PRINT_MAN( ERROR, "intput parameter error!" );
		return -1;
	}
	len = p_model->elem_array[ p_model->cur_index ].len;
	if( len == 0 || len > p_model->buf_len - p_model->cur_position ){
		len = p_model->buf_len - p_model->cur_position;
	}
	return len;
}

int get_packed_len( packet_model p_model )
{
	if( p_model == NULL || p_model->buf == NULL || p_model->is_start == 0 ){
		MOON_PRINT_MAN( ERROR, "input parameter error!" );
		return -1;
	}
	return p_model->buf_len - p_model->cur_position;
}

int set_packet_elem_data_position( packet_model p_model, void * data, int position )
{
	int next;

	if( p_model == NULL ){
		MOON_PRINT_MAN( ERROR, "intput parameter error!" );
		return -1;
	}
	next = get_packet_position( p_model, position );
	if( next < 0 ){
		return -1;
	}
	p_model->elem_array[ next ].p_data = data;
	return 0;
}

void  *  get_packet_elem_data_position( packet_model p_model, int position )
{
	int next;

	if( p_model == NULL ){
		MOON_PRINT_MAN( ERROR, "intput parameter error!" );
		return NULL;
	}
	next = get_packet_position( p_model, position );
	if( next < 0 ){
		return NULL;
	}
	return p_model->elem_array[ next ].p_data;
}

char * get_packet_elem_buf( packet_model p_model )
{
	if( p_model == NULL || p_model->buf == NULL || p_model->is_start == 0 ){
		MOON_PRINT_MAN( ERROR, "intput parameter error!" );
		return NULL;
	}
	return p_model->buf + p_model->cur_position;
}


int next_packet_elem( packet_model p_model )
{
	int next, next_position;

	if( p_model == NULL || p_model->buf == NULL ){
		MOON_PRINT_MAN( ERROR, "input parameter erroor!" );
		return -1;
	}
	if( p_model->is_start == 0 ){
		p_model->is_start = 1;
		return 0;
	}
	next = get_packet_position( p_model, 1 );
	if( next < 0 ){
		return -1;
	}
	if( p_model->direction < 0 ){
		next_position = p_model->cur_position - p_model->elem_array[ next ].len;
	}else{
		next_position = p_model->cur_position + p_model->elem_array[ p_model->cur_index ].len;
	}
	if( next_position < 0 || next_position > p_model->buf_len ){
		MOON_PRINT_MAN( ERROR, "out of buffer range!" );
		return -1;
	}
	p_model->cur_index = next;
	p_model->cur_position = next_position;
	return 0;
}

static inline void _free_packet_data( packet_model p_model )
{
#define FUNC( p_elem ) ({\
	if( ( p_elem )->p_data == NULL ){\
		;\
	}else if( ( p_elem )->data_free != NULL ){\
		( p_elem )->data_free( ( p_elem )->p_data );\
	}else{\
		free( ( p_elem )->p_data );\
	}\
	0;\
})
	ARRAY_TRAVER( p_model->elem_array, p_model->elem_num, FUNC );
#undef FUNC
}

void free_packet_without_buf( packet_model p_model )
{
	if( p_model != NULL ){
		_free_packet_data( p_model );
		free( p_model );
	}
}

void free_packet( packet_model p_model )
{
	if( p_model != NULL ){
		_free_packet_data( p_model );
		IF_FREE( p_model->buf );
		free( p_model );
	}
}

int process_packet( packet_model p_model )
{
	int ( * func )( char *, int );
	int ret;

	if( p_model == NULL || p_model->buf == NULL ){
		MOON_PRINT_MAN( ERROR, "input parameter error!" );
		return -1;
	}
	while( next_packet_elem( p_model ) >= 0 ){
		func = p_model->elem_array[ p_model->cur_index ].process_func[ ( p_model->direction + 2 ) % 3 ];
		if( func != NULL ){
			ret = func( get_packet_elem_buf( p_model ), get_packet_elem_len( p_model ) );
			if( ret < 0 ){
				return -1;
			}
		}
	}
	return 0;
}

