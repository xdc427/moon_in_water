#include<stdio.h>
#include<regex.h>
#include<stdlib.h>
#include<string.h>
#include"shadow_struct.h"
#include"shadow_base.h"
#include"moon_debug.h"
#include"moon_common.h"
#include<openssl/md5.h>

int find_type( struct_all head, char * type )
{
	regex_t regex;
	int err;
	struct_one * one_arr;
	int i;

	if( head == NULL || type == NULL ){
		return -1;
	}
	one_arr = ( struct_one * )( head + 1 );

	for( i = 0; i < head->num; i++){
		err = regcomp( &regex, one_arr[ i ]->name, REG_EXTENDED );
		if( err != 0){
			//need print err info
			return -1;
		}
		err = regexec( &regex, type, 0, NULL, 0 );
		regfree( &regex );
		if( err == 0 ){
			return i;
		}else if( err == REG_NOMATCH ){
			continue;
		}else{
			//need print err info
			return -1;
		}
	}
	return -1;
}

int get_point_num( struct_all p_all, int index )
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

void cal_structs_md5( struct_all p_head )
{
	char * src;
	unsigned long len;

	src = ( char * )( p_head + 1 ) + p_head->num * sizeof( struct_one );
	len = p_head->size - ( src - ( char * )p_head);
	MD5( src, len, p_head->md5_sum );
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

	p_one_src = ( ( struct_one * )( p_copy->src_type + 1 ) )[ p_copy->index ];
	p_one_dst = ( ( struct_one * )( p_copy->dst_type + 1 ) )[ p_copy->index ];
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
		return -1;
	}
	p_one_dst = ( ( struct_one * )( p_copy->dst_type + 1 ) )[ p_copy->index ];
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
	unsigned char * pc_src, * pc_dst, ctmp;

	little_flag = is_little();
	p_one_src = ( ( struct_one * )( p_copy->src_type + 1 ) )[ p_copy->index ];
	p_one_dst = ( ( struct_one * )( p_copy->dst_type + 1 ) )[ p_copy->index ];
	
	if( p_one_src->size == p_one_dst->size && little_flag ){
		memcpy( p_copy->dst, p_copy->src, p_copy->num * p_one_dst->size );
		return 0;
	}
	cp_len = p_one_dst->size > p_one_src->size ? p_one_src->size : p_one_dst->size;	
	if( little_flag ){
		for( i = 0; i < p_copy->num; i++ ){
			for( j = 0; j < cp_len; j++ ){
				( ( char * )p_copy->dst )[ j ] = ( ( char * )p_copy->src )[ j ];
			}
		}
	}else if( p_copy->flag == 0 ){
		for( i = 0; i < p_copy->num; i++ ){
			pc_src = ( char * )p_copy->src + p_one_src->size - 1;
			for( j = 0; j < cp_len; j++ ){
				( ( char * )p_copy->dst )[ j ] = pc_src[ -j ];
			}			
		}
	}else{
		for( i = 0; i < p_copy->num; i++ ){
			pc_dst = ( char * )p_copy->dst + p_one_dst->size - 1;
			for( j = 0; j< cp_len; j++ ){
				pc_dst[ -j ] = ( ( char * )p_copy->src )[ j ];
			}
		}
	}
	return 0;
}

int signed_int_copy( block_copy p_copy )
{
	struct_one p_one_src, p_one_dst;	
	int little_flag, i, j, cp_len;
	unsigned char * pc_src, * pc_dst, ctmp;

	little_flag = is_little();
	p_one_src = ( ( struct_one * )( p_copy->src_type + 1 ) )[ p_copy->index ];
	p_one_dst = ( ( struct_one * )( p_copy->dst_type + 1 ) )[ p_copy->index ];
	
	if( p_one_src->size == p_one_dst->size && little_flag ){
		memcpy( p_copy->dst, p_copy->src, p_copy->num * p_one_dst->size );
		return 0;
	}
	cp_len = p_one_dst->size > p_one_src->size ? p_one_src->size : p_one_dst->size;	
	if( little_flag ){
		for( i = 0; i < p_copy->num; i++ ){
			pc_src = ( unsigned char * )p_copy->src;
			pc_dst = ( unsigned char * )p_copy->dst;
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
			pc_src = ( unsigned char * )p_copy->src + p_one_src->size - 1;
			pc_dst = ( unsigned char * )p_copy->dst;
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
			pc_src = ( unsigned char * )p_copy->src;
			pc_dst = ( unsigned char * )p_copy->dst + p_one_dst->size - 1;
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
	if( p_copy->flag == 0 ){
		memcpy( p_copy->points, p_copy->src, p_copy->num * sizeof( void * ) );
	}else{
		memcpy( p_copy->dst, p_copy->points, p_copy->num * sizeof( void * ) );
	}
	return 0;
}

int float_copy( block_copy p_copy )
{
	struct_one p_one_src, p_one_dst;	
	int little_flag, i, j, cp_len;
	unsigned char * pc_src, * pc_dst, ctmp;

	p_one_src = ( ( struct_one * )( p_copy->src_type + 1 ) )[ p_copy->index ];
	p_one_dst = ( ( struct_one * )( p_copy->dst_type + 1 ) )[ p_copy->index ];
	little_flag = is_little();

	MOON_PRINT_MAN( WARNNING, "float numbers copy dont't checking");
	if( p_one_src->size != p_one_dst->size ){
		MOON_PRINT_MAN( ERROR, "cant't copy sudh float numbres!");
		return -1;
	}else if( little_flag ){
		memcpy( p_copy->dst, p_copy->src, p_copy->num * p_one_dst->size );
	}else if( p_copy->flag == 0 ){
		for( i = 0; i < p_copy->num; i++ ){
			pc_src = ( char * )p_copy->src + p_one_src->size - 1;
			for( j = 0; j < cp_len; j++ ){
				( ( char * )p_copy->dst )[ j ] = pc_src[ -j ];
			}			
		}
	}else{
		for( i = 0; i < p_copy->num; i++ ){
			pc_dst = ( char * )p_copy->dst + p_one_dst->size - 1;
			for( j = 0; j< cp_len; j++ ){
				pc_dst[ -j ] = ( ( char * )p_copy->src )[ j ];
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
		return -1;
	}

	dst_offset = 0;
	while( offset > 0 ){
		p_src_one = ( ( struct_one * )( p_src_all + 1 )	)[ type_index ];
		p_dst_one = ( ( struct_one * )( p_dst_all + 1 ) )[ type_index ];
		if( offset >= p_src_one->size * num ){
			return -1;
		}
		tmp = offset / p_src_one->size;
		dst_offset += tmp * p_dst_one->size;
		offset %= p_src_one->size;
		if( p_src_one->num == 0 ){//基本类型
			if( offset != 0 ){
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
		return -1;//指向了填充数据或hide数据。
	}	
	return dst_offset;
}

int get_struct_len( struct_all p_all, int type_index )
{
	if( type_index < 0 || p_all->num <= type_index ){
		return -1;
	}
	return ( ( struct_one * )( p_all + 1 ) )[ type_index ]->size;
}

int get_struct_points_num( struct_all p_all, int type_index )
{
	if( type_index < 0 || p_all->num <= type_index ){
		return -1;
	}
	return ( ( struct_one * )( p_all + 1 ) )[ type_index ]->point_num;
}

