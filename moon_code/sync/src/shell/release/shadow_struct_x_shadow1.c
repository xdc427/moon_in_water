#include"shadow_struct.h"
#include"shadow_base.h"
#include"structs_x_shadow1.h"
#include<stdio.h>

static char buffer_x_shadow1[ sizeof( struct_all_s ) + ( sizeof( struct_one ) + sizeof( struct_one_s ) ) * 15 + sizeof( var_s ) * 2 ] = { 0 };

struct_all shadow_struct_x_shadow1_init()
{
	struct_all struct_head;
	struct_one * one_arr;
	struct_one tmp_one;
	var tmp_var;
	int i;

	struct_head = ( struct_all )buffer_x_shadow1;
	struct_head->size = sizeof( buffer_x_shadow1 );
	struct_head->num = 15;
	one_arr = ( struct_one * )(	struct_head + 1 );
	tmp_one = ( struct_one )( one_arr + 15 );
	snprintf( tmp_one->name , sizeof( tmp_one->name ), "\\*" );
	tmp_one->size = sizeof( void * );
	tmp_one->num = 0;
	tmp_one->point_num = 0;
	tmp_one->copy_func = point_copy;
	*one_arr++ = tmp_one;
	tmp_one++;
	snprintf( tmp_one->name , sizeof( tmp_one->name ), "\\bunsigned\\b.*\\bchar\\b" );
	tmp_one->size = sizeof( unsigned char );
	tmp_one->num = 0;
	tmp_one->point_num = 0;
	tmp_one->copy_func = unsigned_int_copy;
	*one_arr++ = tmp_one;
	tmp_one++;
	snprintf( tmp_one->name , sizeof( tmp_one->name ), "\\bchar\\b" );
	tmp_one->size = sizeof( char );
	tmp_one->num = 0;
	tmp_one->point_num = 0;
	tmp_one->copy_func = signed_int_copy;
	*one_arr++ = tmp_one;
	tmp_one++;
	snprintf( tmp_one->name , sizeof( tmp_one->name ), "\\bunsigned\\b.*\\bshort\\b" );
	tmp_one->size = sizeof( unsigned short );
	tmp_one->num = 0;
	tmp_one->point_num = 0;
	tmp_one->copy_func = unsigned_int_copy;
	*one_arr++ = tmp_one;
	tmp_one++;
	snprintf( tmp_one->name , sizeof( tmp_one->name ), "\\bshort\\b" );
	tmp_one->size = sizeof( short );
	tmp_one->num = 0;
	tmp_one->point_num = 0;
	tmp_one->copy_func = signed_int_copy;
	*one_arr++ = tmp_one;
	tmp_one++;
	snprintf( tmp_one->name , sizeof( tmp_one->name ), "\\bunsigned\\b.*\\blong\\b.*\\blong\\b" );
	tmp_one->size = sizeof( unsigned long long );
	tmp_one->num = 0;
	tmp_one->point_num = 0;
	tmp_one->copy_func = unsigned_int_copy;
	*one_arr++ = tmp_one;
	tmp_one++;
	snprintf( tmp_one->name , sizeof( tmp_one->name ), "\\blong\\b.*\\blong\\b" );
	tmp_one->size = sizeof( long long );
	tmp_one->num = 0;
	tmp_one->point_num = 0;
	tmp_one->copy_func = signed_int_copy;
	*one_arr++ = tmp_one;
	tmp_one++;
	snprintf( tmp_one->name , sizeof( tmp_one->name ), "\\bunsigned\\b.*\\blong\\b" );
	tmp_one->size = sizeof( unsigned long );
	tmp_one->num = 0;
	tmp_one->point_num = 0;
	tmp_one->copy_func = unsigned_int_copy;
	*one_arr++ = tmp_one;
	tmp_one++;
	snprintf( tmp_one->name , sizeof( tmp_one->name ), "\\blong\\b" );
	tmp_one->size = sizeof( long );
	tmp_one->num = 0;
	tmp_one->point_num = 0;
	tmp_one->copy_func = signed_int_copy;
	*one_arr++ = tmp_one;
	tmp_one++;
	snprintf( tmp_one->name , sizeof( tmp_one->name ), "\\bunsigned\\b" );
	tmp_one->size = sizeof( unsigned );
	tmp_one->num = 0;
	tmp_one->point_num = 0;
	tmp_one->copy_func = unsigned_int_copy;
	*one_arr++ = tmp_one;
	tmp_one++;
	snprintf( tmp_one->name , sizeof( tmp_one->name ), "\\bint\\b|\\bsigned\\b" );
	tmp_one->size = sizeof( int );
	tmp_one->num = 0;
	tmp_one->point_num = 0;
	tmp_one->copy_func = signed_int_copy;
	*one_arr++ = tmp_one;
	tmp_one++;
	snprintf( tmp_one->name , sizeof( tmp_one->name ), "\\blong\\b.*\\bdouble\\b" );
	tmp_one->size = sizeof( long double );
	tmp_one->num = 0;
	tmp_one->point_num = 0;
	tmp_one->copy_func = float_copy;
	*one_arr++ = tmp_one;
	tmp_one++;
	snprintf( tmp_one->name , sizeof( tmp_one->name ), "\\bdouble\\b" );
	tmp_one->size = sizeof( double );
	tmp_one->num = 0;
	tmp_one->point_num = 0;
	tmp_one->copy_func = float_copy;
	*one_arr++ = tmp_one;
	tmp_one++;
	snprintf( tmp_one->name , sizeof( tmp_one->name ), "\\bfloat\\b" );
	tmp_one->size = sizeof( float );
	tmp_one->num = 0;
	tmp_one->point_num = 0;
	tmp_one->copy_func = float_copy;
	*one_arr++ = tmp_one;
	tmp_one++;
	tmp_one[ -14 ].point_num = 1;
	tmp_var = ( var )( tmp_one ) - 1;
	tmp_one = ( struct_one )( tmp_var + 1 );
	snprintf( tmp_one->name , sizeof( tmp_one->name ), "\\blist_shadow1_s\\b" );
	tmp_one->size = sizeof( struct list_shadow1_s );
	tmp_one->num = 2;
	tmp_one->point_num = -1;
	tmp_one->copy_func = NULL;
	*one_arr++ = tmp_one;
	tmp_var = ( var )( tmp_one + 1 ) - 1;
	tmp_var++;
	snprintf( tmp_var->name, sizeof( tmp_var->name ), "id" );
	snprintf( tmp_var->type, sizeof( tmp_var->type ), "int" );
	tmp_var->size = sizeof( ( ( struct list_shadow1_s * )0 )->id );
	tmp_var->addr = &( ( ( struct list_shadow1_s * )0 )->id );
	tmp_var->array_d = 0;
	tmp_var->array_elem_size = sizeof( int );
	tmp_var->len = tmp_var->size / tmp_var->array_elem_size;
	tmp_var->is_hide = 0;
	tmp_var++;
	snprintf( tmp_var->name, sizeof( tmp_var->name ), "next" );
	snprintf( tmp_var->type, sizeof( tmp_var->type ), "struct list_shadow1_s *" );
	tmp_var->size = sizeof( ( ( struct list_shadow1_s * )0 )->next );
	tmp_var->addr = &( ( ( struct list_shadow1_s * )0 )->next );
	tmp_var->array_d = 0;
	tmp_var->array_elem_size = sizeof( struct list_shadow1_s * );
	tmp_var->len = tmp_var->size / tmp_var->array_elem_size;
	tmp_var->is_hide = 0;
	complete_var_type( struct_head );
	return struct_head;
}
inline void * get_x_shadow1_head_addr()
{
	return &head;
}

inline unsigned long get_x_shadow1_head_size()
{
	return sizeof( head );
}

inline unsigned long get_x_shadow1_head_elem_size()
{
	return sizeof( struct list_shadow1_s * );
}

inline unsigned long get_x_shadow1_head_len()
{
	return sizeof( head ) / sizeof( struct list_shadow1_s * );
}


