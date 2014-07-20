#include<stdio.h>
#include<regex.h>
#include<stdlib.h>
#include<string.h>
#include"shadow_struct.h"
#include"shadow_base.h"
#include"moon_debug.h"
#include"moon_common.h"
#include<openssl/md5.h>

#define GET_STRUCT_ONE( p_all, index ) ( ( ( struct_one * )( ( p_all ) + 1 ) )[ index ] )

int find_type( struct_all head, char * type )
{
	regex_t regex;
	int err;
	struct_one * one_arr;
	int i;

	if( head == NULL || type == NULL ){
		MOON_PRINT_MAN( ERROR, "inout paramers error!" );
		return -1;
	}
	one_arr = ( struct_one * )( head + 1 );

	for( i = 0; i < head->num; i++){
		err = regcomp( &regex, one_arr[ i ]->name, REG_EXTENDED );
		if( err != 0){
			MOON_PRINT_MAN( ERROR, "regex compile error!" );
			return -1;
		}
		err = regexec( &regex, type, 0, NULL, 0 );
		regfree( &regex );
		if( err == 0 ){
			return i;
		}else if( err == REG_NOMATCH ){
			continue;
		}else{
			MOON_PRINT_MAN( ERROR, "regex match error!" );
			return -1;
		}
	}
	MOON_PRINT_MAN( ERROR, "can't find such type:%s", type );
	return -1;
}

static inline int get_point_num( struct_all p_all, int index )
{
	struct_one p_one;
	var p_var;
	int i;

	p_one = ( ( struct_one * )( p_all + 1 ) )[ index ];
	if( p_one->point_num >= 0 ){
		return p_one->point_num;
	}
	p_one->point_num = 0;
	for( i = 0; i < p_one->num; i++ ){
		p_var = &( ( var )( p_one + 1 ) )[ i ];
		p_var->point_addr = p_one->point_num;
		p_one->point_num += p_var->len* get_point_num( p_all, p_var->type_num);
	}
	return p_one->point_num;
}

static inline void cal_structs_md5( struct_all p_head )
{
	char * src;
	unsigned long len;

	src = ( char * )( p_head + 1 ) + p_head->num * sizeof( struct_one );
	len = p_head->size - ( src - ( char * )p_head);
	MD5( ( unsigned char * )src, len, p_head->md5_sum );
}

struct_all dump_struct_all( struct_all p_all )
{
	struct_all p_all_new;
	struct_one * pp_one_src, * pp_one_dst;
	struct_one p_one_start;
	int i;

	if( p_all == NULL || p_all->num <= 0 ){
		return NULL;
	}
	if( ( p_all_new = ( struct_all )malloc( p_all->size ) ) == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!");
		return NULL;
	}
	memcpy( p_all_new, p_all, p_all->size );
	p_one_start = ( struct_one )( ( struct_one * )( p_all_new + 1 ) + p_all_new->num );
	pp_one_src = ( struct_one * )( p_all + 1 );
	pp_one_dst = ( struct_one * )( p_all_new + 1 );
	pp_one_dst[ 0 ] = p_one_start;
	for( i = 1; i < p_all_new->num; i++ ){
		pp_one_dst[ i ] = ( struct_one )( ( char * )pp_one_dst[ i - 1 ] 
				+ ( ( char * )pp_one_src[ i ] - ( char * )pp_one_src[ i - 1 ] ) );
	}
	return p_all_new;
}

void free_struct_all( struct_all p_all )
{
	IF_FREE( p_all );
}

void complete_var_type( struct_all head )
{
	struct_one * one_arr;
	struct_one tmp_one;
	var tmp_var;
	int i, j, id;

	one_arr = ( struct_one * )( head + 1 );

	for( i = 0; i < head->num; i++ ){
		tmp_one = one_arr[ i ];
		for( j =0; j < tmp_one->num; j++ ){
			tmp_var = ( var )( tmp_one + 1 );
			id = find_type( head, tmp_var->type );
			if( id < 0 ){
				MOON_PRINT_MAN( ERROR, "this struct type not defined:%s", tmp_var->type );
				return;
			}
			tmp_var->type_num = id;
		}
	}
	//统计每个结构体的指针数量。
	for( i = 0; i < head->num; i++ ){
		get_point_num( head, i);
	}
	cal_structs_md5( head );
}

static int _get_block_copy( block_copy p_copy , int only_points )
{
	int i, j, n_ret;
	struct_one p_one_src, p_one_dst;
	var p_var_src, p_var_dst;
	block_copy_s copy;

	p_one_src = GET_STRUCT_ONE( p_copy->src_type, p_copy->index );
	p_one_dst = GET_STRUCT_ONE( p_copy->dst_type, p_copy->index );
	if( only_points && p_one_src->point_num == 0 ){
		return 0;
	}else if( p_one_src->copy_func != NULL ){
		return p_one_src->copy_func( p_copy );
	}
	
	for( i = 0; i < p_copy->num; i++ ){
		for( j = 0; j < p_one_src->num; j++ ){
			p_var_src = &( ( var )( p_one_src + 1 ) )[ j ];
			p_var_dst = &( ( var )( p_one_dst + 1 ) )[ j ];
			copy.src = p_copy->src + i * p_one_src->size + p_var_src->addr;
			copy.dst = p_copy->dst + i * p_one_dst->size + p_var_dst->addr;
			copy.points = p_copy->points + i * p_one_src->point_num + p_var_src->point_addr;
			copy.index = p_var_src->type_num;
			copy.num = p_var_src->size / p_var_src->array_elem_size;
			copy.src_type = p_copy->src_type;
			copy.dst_type = p_copy->dst_type;
			copy.flag = p_copy->flag;
			n_ret = _get_block_copy( &copy, only_points );
			if( n_ret < 0 ){
				return n_ret;
			}
		}
	}
	return 0;
}

int get_block_copy( block_copy p_copy )
{
	struct_one p_one_dst;

	if( p_copy->index < 0 || p_copy->index >= p_copy->src_type->num ){
		MOON_PRINT_MAN( ERROR, "input paramers error!" );
		return -1;
	}
	p_one_dst = GET_STRUCT_ONE( p_copy->dst_type, p_copy->index );
	if( memcmp( p_copy->src_type->md5_sum, p_copy->dst_type->md5_sum 
			, sizeof( p_copy->src_type->md5_sum ) ) == 0 ){
		//源和目标的编译情况一模一样，只需要进行指针转换即可。
		memcpy( p_copy->dst, p_copy->src, p_one_dst->size * p_copy->num );
		return _get_block_copy( p_copy, 1 );
	}else{
		return _get_block_copy( p_copy, 0 );
	}
}

int unsigned_int_copy( block_copy p_copy )
{
	struct_one p_one_src, p_one_dst;	
	int little_flag, i, j, cp_len;
	unsigned char * pc_src, * pc_dst;

	little_flag = is_little();
	p_one_src = GET_STRUCT_ONE( p_copy->src_type, p_copy->index );
	p_one_dst = GET_STRUCT_ONE( p_copy->dst_type, p_copy->index );
	
	if( p_one_src->size == p_one_dst->size && little_flag ){
		memcpy( p_copy->dst, p_copy->src, p_copy->num * p_one_dst->size );
		return 0;
	}
	cp_len = p_one_dst->size > p_one_src->size ? p_one_src->size : p_one_dst->size;	
	if( little_flag ){
		for( i = 0; i < p_copy->num; i++ ){
			pc_src = ( unsigned char * )p_copy->src + p_one_src->size * i;
			pc_dst = ( unsigned char * )p_copy->dst + p_one_dst->size * i;
			for( j = 0; j < cp_len; j++ ){
				pc_dst[ j ] = pc_src[ j ];
			}
		}
	}else if( p_copy->flag == 0 ){
		for( i = 0; i < p_copy->num; i++ ){
			pc_src = ( unsigned char * )p_copy->src + p_one_src->size * ( i + 1 ) - 1;
			pc_dst = ( unsigned char * )p_copy->dst + p_one_dst->size * i;
			for( j = 0; j < cp_len; j++ ){
				pc_dst[ j ] = pc_src[ -j ];
			}			
		}
	}else{
		for( i = 0; i < p_copy->num; i++ ){
			pc_src = ( unsigned char * )p_copy->src + p_one_src->size * i;
			pc_dst = ( unsigned char * )p_copy->dst + p_one_dst->size * ( i + 1 ) - 1;
			for( j = 0; j< cp_len; j++ ){
				pc_dst[ -j ] = pc_src[ j ];
			}
		}
	}
	return 0;
}

int signed_int_copy( block_copy p_copy )
{
	struct_one p_one_src, p_one_dst;	
	int little_flag, i, j, cp_len;
	unsigned char * pc_src, * pc_dst;

	little_flag = is_little();
	p_one_src = GET_STRUCT_ONE( p_copy->src_type, p_copy->index );
	p_one_dst = GET_STRUCT_ONE( p_copy->dst_type, p_copy->index );
	
	if( p_one_src->size == p_one_dst->size && little_flag ){
		memcpy( p_copy->dst, p_copy->src, p_copy->num * p_one_dst->size );
		return 0;
	}
	cp_len = p_one_dst->size > p_one_src->size ? p_one_src->size : p_one_dst->size;	
	if( little_flag ){
		for( i = 0; i < p_copy->num; i++ ){
			pc_src = ( unsigned char * )p_copy->src + p_one_src->size * i;
			pc_dst = ( unsigned char * )p_copy->dst + p_one_dst->size * i;
			for( j = 0; j < cp_len; j++ ){
				pc_dst[ j ] = pc_src[ j ];
			}
			if( p_one_dst->size > cp_len && pc_src[ cp_len - 1 ] & 0x80 ){//符号扩展
				for( j = cp_len; j < p_one_dst->size; j++ ){
					pc_dst[ j ] = 0xff;
				}
			}
		}
	}else if( p_copy->flag == 0 ){//大端转小端
		for( i = 0; i < p_copy->num; i++ ){
			pc_src = ( unsigned char * )p_copy->src + p_one_src->size * ( i + 1 ) - 1;
			pc_dst = ( unsigned char * )p_copy->dst + p_one_dst->size * i;
			for( j = 0; j < cp_len; j++ ){
				pc_dst[ j ] = pc_src[ -j ];
			}			
			if( p_one_dst->size > cp_len && pc_src[ 0 ] & 0x80 ){
				for( j = cp_len; j < p_one_dst->size; j++ ){
					pc_dst[ j ] = 0xff;
				}
			}
		}
	}else{//小端转大端
		for( i = 0; i < p_copy->num; i++ ){
			pc_src = ( unsigned char * )p_copy->src + p_one_src->size * i;
			pc_dst = ( unsigned char * )p_copy->dst + p_one_dst->size * ( i + 1 ) - 1;
			for( j = 0; j < cp_len; j++ ){
				pc_dst[ -j ] = pc_src[ j ];
			}
			if( p_one_dst->size > cp_len && pc_src[ cp_len - 1 ] & 0x80 ){
				for( j = cp_len; j < p_one_dst->size; j++ ){
					pc_dst[ -j ] = 0xff;
				}
			}
		}
	}
	return 0;
}

int point_copy( block_copy p_copy )
{
	struct_one p_one_dst;

	if( p_copy->flag == 0 ){
		p_one_dst = GET_STRUCT_ONE( p_copy->dst_type, p_copy->index );
		memcpy( p_copy->points, p_copy->src, p_copy->num * sizeof( void * ) );
		memset( p_copy->dst, 0, p_copy->num * p_one_dst->size );
	}else{
		memcpy( p_copy->dst, p_copy->points, p_copy->num * sizeof( void * ) );
	}
	return 0;
}

int float_copy( block_copy p_copy )
{
	struct_one p_one_src, p_one_dst;	
	int little_flag, i, j, cp_len;
	unsigned char * pc_src, * pc_dst;

	p_one_src = GET_STRUCT_ONE( p_copy->src_type, p_copy->index );
	p_one_dst = GET_STRUCT_ONE( p_copy->dst_type, p_copy->index );
	little_flag = is_little();
	cp_len = p_one_src->size;
	MOON_PRINT_MAN( WARNNING, "float numbers copy dont't checking");
	if( p_one_src->size != p_one_dst->size ){
		MOON_PRINT_MAN( ERROR, "cant't copy sudh float numbres!");
		return -1;
	}else if( little_flag ){
		memcpy( p_copy->dst, p_copy->src, p_copy->num * p_one_dst->size );
	}else if( p_copy->flag == 0 ){
		for( i = 0; i < p_copy->num; i++ ){
			pc_src = ( unsigned char * )p_copy->src + p_one_src->size * ( i + 1 ) - 1;
			pc_dst = ( unsigned char * )p_copy->dst + p_one_dst->size * i;
			for( j = 0; j < cp_len; j++ ){
				pc_dst[ j ] = pc_src[ -j ];
			}			
		}
	}else{
		for( i = 0; i < p_copy->num; i++ ){
			pc_src = ( unsigned char * )p_copy->src + p_one_src->size * i;
			pc_dst = ( unsigned char * )p_copy->dst + p_one_dst->size * ( i + 1 )- 1;
			for( j = 0; j< cp_len; j++ ){
				pc_dst[ -j ] = pc_src[ j ];
			}
		}
	}
	return 0;
}

int get_target_offset( struct_all p_src_all, struct_all p_dst_all, int offset, int type_index, int num )
{
	struct_one p_src_one, p_dst_one;
	int tmp, i, dst_offset;
	var p_src_var, p_dst_var;

	if( offset < 0 || num < 0 || type_index < 0 || type_index >= p_src_all->num ){
		MOON_PRINT_MAN( ERROR, "input paramers error!" );
		return -1;
	}

	dst_offset = 0;
	while( offset > 0 ){
		p_src_one = GET_STRUCT_ONE( p_src_all, type_index );
		p_dst_one = GET_STRUCT_ONE( p_dst_all, type_index );
		if( offset >= p_src_one->size * num ){
			MOON_PRINT_MAN( ERROR, "offset out of range!" );
			return -1;
		}
		tmp = offset / p_src_one->size;
		dst_offset += tmp * p_dst_one->size;
		offset %= p_src_one->size;
		if( p_src_one->num == 0 ){//基本类型
			if( offset != 0 ){
				MOON_PRINT_MAN( ERROR, "offset not in boundary" );
				return -1;
			}else{
				break;
			}
		}
		p_src_var = ( var )( p_src_one + 1 );
		p_dst_var = ( var )( p_dst_one + 1 );
		tmp = 0;
		for( i = 0; i < p_src_one->num; i++ ){
			if( offset < p_src_var[ i ].addr ){
				break;
			}
			if( p_src_var[ i ].addr <= offset 
					&& p_src_var[ i ].addr + p_src_var[ i ].size < offset ){
				if( p_src_var[ i ].is_hide > 0 ){
					break;;
				}
				dst_offset += p_dst_var[ i ].addr;
				offset -= p_src_var[ i ].addr;
				num = p_src_var[ i ].len;
				type_index = p_src_var[ i ].type_num;
				continue;
			}
		}
		MOON_PRINT_MAN( ERROR, "offset at hide range!" );
		return -1;//指向了填充数据或hide数据。
	}	
	return dst_offset;
}

int get_struct_len( struct_all p_all, int type_index )
{
	if( type_index < 0 || p_all->num <= type_index ){
		return -1;
	}
	return GET_STRUCT_ONE( p_all, type_index )->size;
}

int get_struct_points_num( struct_all p_all, int type_index )
{
	if( type_index < 0 || p_all->num <= type_index ){
		return -1;
	}
	return GET_STRUCT_ONE( p_all, type_index )->point_num;
}

static inline int print_var( var p_var )
{
	printf( "var:name:%s:type:%s:type_id:%d ->\n"
		"addr:%d:size:%d:len:%d:elem_size:%d:point_addr:%d:is_hide:%d\n"
		, p_var->name, p_var->type, p_var->type_num, p_var->addr, p_var->size
		, p_var->len, p_var->array_elem_size, p_var->point_addr, p_var->is_hide );
	return 0;
}
static inline int print_struct_one( struct_one * pp_one )
{
	struct_one p_one = *pp_one;

	printf( "struct:name:%s:size:%d:var_num:%d:point_num:%d:\n"
		, p_one->name, p_one->size, p_one->num, p_one->point_num );	
	ARRAY_TRAVER( ( var )( p_one + 1 ), p_one->num, print_var );
	return 0;
}

void print_struct_all( struct_all p_all )
{
	if( p_all == NULL ){
		return;
	}
	printf( "----------------\nmd5:" );
	print_binary( p_all->md5_sum, sizeof( p_all->md5_sum ) );
	printf( "\nnum:%d\nsize:%d\n\n", p_all->num, p_all->size );
	ARRAY_TRAVER( ( struct_one * )( p_all + 1 ), p_all->num, print_struct_one );
	printf( "----------------\n");
}

#ifdef LEVEL_TEST
void set_type_size( struct_all p_all, char * type , int size )
{
	int index;
	

	if( p_all == NULL || type == NULL || size == 0 ){
		return;
	}
	if( ( index = find_type( p_all, type ) ) < 0 ){
		return;
	}
	GET_STRUCT_ONE( p_all, index )->size = size;
}
#endif

/*将struct_all转化为一块可传输的内存，把所有的类型长度放前，再依次放ar地址：
 *struct_one:
 *	size: 4B
 *struct_var:
 *	addr: 4B
*/
static inline int get_struct_all_to_buf_len( struct_all p_all )
{
	int size, i;
	struct_one * pp_one;

	size = 0;
	pp_one = ( struct_one * )( p_all + 1 );
	for( i = 0; i < p_all->num; i++ ){
		size += ( 1 + pp_one[ i ].num ) * sizeof( uint32_t );
	}
	return size;
}

int convert_struct_all_to_buf( struct_all p_all, char ** p_buf, int * p_size )
{
	int size, i, j;
	struct_one * pp_one;
	var p_var;
	char * buf;
	uint32_t * p_u32, * p_var_offset;

	if( p_all == NULL || p_buf == NULL || p_size == NULL ){
		return -1;
	}
	pp_one = ( struct_one * )( p_all + 1 );
	size = get_struct_all_to_buf_len( p_all );
	if( ( buf = malloc( size ) ) == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		return -1;
	}
	p_u32 = ( uint32_t *)buf;
	p_var_offset = p_u32 + p_all->num;
	for( i = 0; i < p_all->num; i++ ){
		*p_u32 = pp_one[ i ].size;
		convert_uint32_t( p_u32 );
		p_u32++;
		for( j = 0; j < pp_one[ i ].num; j++ ){
			p_var = ( var )( pp_one[ i ] + 1 );
			*p_var_offset = p_var->addr;
			convert_uint32_t( p_var_offset );
			p_var_offset++;
		}
	}
	*p_buf = buf;
	*p_size = size;
	return 0;
}

int convert_buf_to_struct_all( struct_all p_src_all, struct_all * pp_dst_all, char * buf, int size )
{
	int i, j;
	struct_all p_dst_all;
	struct_one * pp_dst_one;
	var p_var;
	uint32_t * p_u32;

	if( p_src_all == NULL || pp_dst_all == NULL || buf == NULL
			|| size < get_struct_all_to_buf_len( p_src_all ) ){
		return -1;
	}
	p_dst_all = dump_struct_all( p_src_all );
	if( p_dst_all == NULL ){
		MOON_PRINT_MAN( ERROR, "sump struct all error!" );
		return -1;
	}
	pp_dst_one = ( struct_one * )( p_dst_all + 1 );
	p_u32 = ( uint32_t * )buf;
	for( i = 0; i < p_dst_all->num; i++ ){
		convert_uint32_t( p_u32 );
		pp_dst_one[ i ].size = *p_u32;
		p_u32++;
	}
	for( i = 0; i < p_dst_all->num; i++ ){
		for( j = 0; j < pp_dst_one[ i ].num; j++ ){
			p_var = ( var )( pp_dst_one[ i ] + 1 );
			convert_uint32_t( p_u32 );
			p_var->addr = *p_u32;
			p_u32++;
			p_var->array_elem_size = pp_dst_one[ p_var->type_num ].size;
			p_var->size = p_var->len * p_var->array_elem_size;
		}
	}
	cal_structs_md5( p_dst_all );
	*pp_dst_all = p_dst_all;
	return 0;
}

