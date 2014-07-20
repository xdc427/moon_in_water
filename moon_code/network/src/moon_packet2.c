#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<pthread.h>
#include"moon_packet2.h"
#include"moon_common.h"
#include"moon_debug.h"

//上下各留8块空闲的
#define FREE_DESC_NUM ( 8 )
#define TOTAL_FREE_DESC_NUM ( FREE_DESC_NUM * 2 )
#define get_buf_desc( p_head, index ) ( ( buf_desc )( p_head + 1 ) + index )

typedef struct{
	int ref_num;//use case sync
} packet_buf_s;
typedef packet_buf_s * packet_buf;

//buf_head_s + [ buf_desc_s ]
typedef struct{
	int buf_len;
	int cur_index;
	int last_index;
	int buf_num;
	//travel
	int traver_next_index;
	//split
	int split_next_index;
	int split_len;
} buf_head_s;

void * packet_buf_malloc( int size )
{
	packet_buf p_buf;

	if( size <= 0 ){
		return NULL;
	}
	p_buf = malloc( sizeof( *p_buf ) + size );
	if( p_buf != NULL ){
		p_buf->ref_num = 1;
	}
	return p_buf->buf;
}

void packet_buf_free( void * buf )
{
	if( buf == NULL ){
		return;
	}
	free( ( packet_buf )buf - 1 );
}

static int _add_buf_to_packet( buf_head * pp_head, buf_desc p_desc, int num, int len )
{
	buf_head p_head, p_malloc_head;
	int already_num;

	p_head = *pp_head;
	if( p_head == NULL ){
		p_head = malloc( sizeof( *p_head ) 
			+ ( num + TOTAL_FREE_DESC_NUM ) * sizeof( buf_desc_s ) );
		if( p_head == NULL ){
			return -1;
		}
		p_head->cur_index = FREE_DESC_NUM;
		p_head->last_index = FREE_DESC_NUM + num - 1;
		p_head->buf_num = TOTAL_FREE_DESC_NUM + num;
		p_head->buf_len = len;
		*pp_head = p_head;
	}else if( p_head->cur_index < num ){
		already_num = p_head->last_index - p_head->cur_index + 1;
		p_malloc_head = malloc( sizeof( *p_malloc_head ) 
			+ ( num + TOTAL_FREE_DESC_NUM + already_num ) * sizeof( buf_desc_s ) );
		if( p_malloc_head == NULL ){
			return -1;
		}
		p_malloc_head->cur_index = FREE_DESC_NUM;
		p_malloc_head->last_index = FREE_DESC_NUM + num + already_num - 1;
		p_malloc_head->buf_num = TOTAL_FREE_DESC_NUM + num + already_num;
		p_malloc_head->buf_len = p_head->buf_len + len;
		mempcpy( get_buf_desc( p_malloc_head, p_malloc_head->cur_index + num )
			, get_buf_desc( p_head, p_head->cur_index ), already_num * sizeof( buf_desc_s ) );
		free( p_head );
		p_head = p_malloc_head;
		*pp_head = p_head;
	}
	mempcpy( get_buf_desc( p_malloc_head, p_malloc_head->cur_index )
		p_desc, num * sizeof( *p_desc ) );
	return 0;
}

int add_buf_to_packet( buf_head * pp_head, buf_desc p_desc )
{
	if( pp_head == NULL || p_desc == NULL
		|| p_desc->buf == NULL || p_desc->offset < 0 || p_desc->len <= 0 ){
		MOON_PRINT_MAN( ERROR, "input parameters error!" );
		return -1;
	}
	return _add_buf_to_packet( pp_head, p_desc, 1, p_desc->len );
}

int get_packet_len( buf_head p_head )
{
	if( p_head == NULL ){
		return 0;
	}
	return p_head->buf_len;
}

void free_total_packet( buf_head p_head )
{
	buf_desc p_desc;
	packet_buf p_buf;
	int tmp, i;

	if( p_head == NULL ){
		return;
	}
	tmp = 0;
	for( i = p_head->cur_index; i <= p_head->last_index; i++ ){
		p_desc = get_buf_desc( p_head, i );
		p_buf = ( packet_buf )p_desc->buf - 1;
		tmp = __sync_sub_and_fetch( &p_buf->ref_num );
		if( tmp == 0 ){
			free( p_buf );
		}else if( tmp < 0 ){
			MOON_PRINT_MAN( ERROR, "buf ref num under overflow!" );
		}
	}
	free( p_head );
}

void begin_travel_packet( buf_head p_head )
{
	if( p_head == NULL ){
		return;
	}
	p_head->traver_next_index = p_head->cur_index;
}

int get_next_buf( buf_head p_head, char ** buf, int * p_len )
{
	buf_desc p_desc;

	if( p_head == NULL || buf == NULL || p_len == NULL ){
		return -1;
	}
	if( p_head->cur_index > p_head->last_index ){
		return 0;
	}
	p_desc = get_buf_desc( p_head, p_head->traver_next_index );
	*buf = p_desc->buf + p_desc->offset;
	*p_len = p_desc->len;
	p_head->traver_next_index++;
	return p_desc->len;
}

void begin_split_packet( buf_head p_head, int len )
{
	buf_desc p_desc;
	int i;

	if( p_head == NULL ){
		return;
	}
	p_head->split_len = 0;
	p_head->split_next_index = p_head->cur_index;
	if( len >= p_head->buf_len ){
		p_head->split_next_index = p_head->last_index + 1;	
	}else if( len > 0 ){
		for( i = p_head->cur_index; i <= p_head->last_index; i++ ){
			p_desc = get_buf_desc( p_head, i );
			if( len < p_desc->len ){
				p_head->split_len = len;
				break;
			}else{
				len -= p_desc->len;
			}
		}
		p_head->split_next_index = i;
	}
}

//len <= 0: to end
int get_next_packet( buf_head p_head, int len, buf_head * pp_head )
{
	buf_head p_malloc_head;
	buf_desc p_desc;
	packet_buf p_buf;
	int left_len, tmp, i, need_bloks;
	int s_split_next_index, s_split_len;

	if( p_head == NULL || pp_head == NULL ){
		return -1;
	}
	if( p_head->split_next_index > p_head->last_index ){//end
		return 0;
	}
	left_len = len;
	if( left_len <= 0 || left_len > p_head->buf_len ){
		left_len = p_head->buf_len;
		need_bloks = p_head->last_index - p_head->split_next_index + 1;
		s_split_next_index = p_head->p_head->last_index + 1;
	}else{
		p_desc = get_buf_desc( p_head, p_head->split_next_index );
		s_split_len = left_len + p_head->split_len;
		tmp = p_desc->len - p_head->split_len;
		for( i = p_head->split_next_index + 1; tmp < left_len; i++ ){
			p_desc = get_buf_desc( p_head, i );
			s_split_len = left_len - tmp;
			tmp += p_desc->len;
		}
		s_split_next_index = i;
		if( tmp > left_len ){
			s_split_next_index--;
		}else{
			s_split_len = 0;
		}
		need_bloks = i - p_head->split_next_index;
	}
	need_bloks += TOTAL_FREE_DESC_NUM;
	p_malloc_head = malloc( sizeof( *p_malloc_head ) + need_bloks * sizeof( buf_desc_s ) );
	if( p_malloc_head == NULL ){
		return -1;
	}
	p_malloc_head->cur_index = FREE_DESC_NUM;
	p_malloc_head->last_index = need_bloks - FREE_DESC_NUM -1;
	p_malloc_head->buf_num = need_bloks;
	p_malloc_head->buf_len = left_len;
	memcpy( get_buf_desc( p_malloc_head, p_malloc_head->cur_index )
		, get_buf_desc( p_head, p_head->split_next_index )
		, sizeof( buf_desc_s ) * ( p_malloc_head->last_index - p_malloc_head->cur_index + 1 ) );
	
	p_desc = get_buf_desc( p_malloc_head, p_malloc_head->cur_index );
	p_desc->offset += p_head->split_len;
	if( s_split_len != 0 ){
		if( p_malloc_head->last_index == p_malloc_head->cur_index ){
			p_desc->len = s_split_len = p_head->split_len;
		}else{
			p_desc->len -= p_head->split_len;
			p_desc = get_buf_desc( p_malloc_head, p_malloc_head->last_index );
			p_desc->len = s_split_len;
		}
	}
	for( i = p_malloc_head->cur_index;i <= p_malloc_head->last_index; i++ ){
		p_desc = get_buf_desc( p_malloc_head, i );
		p_buf = ( packet_buf )p_desc->buf - 1;
		__sync_add_and_fetch( &p_buf->ref_num );
	}
	p_head->split_next_index = s_split_next_index;
	p_head->split_len = s_split_len;
	*pp_head = p_malloc_head;
	return p_malloc_head->buf_len;
}

buf_head dump_packet( buf_head p_head )
{
	buf_head p_new = NULL;

	if( p_head == NULL ){
		return p_new;
	}
	begin_split_packet( p_head, 0 );
	get_next_packet( p_head, 0, &p_new );
	return p_new;
}

static int _append_buf_to_packet( buf_head * pp_head, buf_desc p_desc, int num, int len )
{
	buf_head p_head, p_malloc_head;
	int already_num;

	p_head = *pp_head;
	if( p_head == NULL ){
		p_head = malloc( sizeof( *p_head ) 
			+ ( num + TOTAL_FREE_DESC_NUM ) * sizeof( buf_desc_s ) );
		if( p_head == NULL ){
			return -1;
		}
		p_head->cur_index = FREE_DESC_NUM;
		p_head->last_index = FREE_DESC_NUM + num - 1;
		p_head->buf_num = TOTAL_FREE_DESC_NUM + num;
		p_head->buf_len = len;
		*pp_head = p_head;
	}else if( p_head->buf_num - p_head->last_index - 1 < num ){
		already_num = p_head->last_index - p_head->cur_index + 1;
		p_malloc_head = malloc( sizeof( *p_malloc_head ) 
			+ ( num + TOTAL_FREE_DESC_NUM + already_num ) * sizeof( buf_desc_s ) );
		if( p_malloc_head == NULL ){
			return -1;
		}
		p_malloc_head->cur_index = FREE_DESC_NUM;
		p_malloc_head->last_index = FREE_DESC_NUM + num + already_num - 1;
		p_malloc_head->buf_num = TOTAL_FREE_DESC_NUM + num + already_num;
		p_malloc_head->buf_len = p_head->buf_len + len;
		mempcpy( get_buf_desc( p_malloc_head, p_malloc_head->cur_index )
			, get_buf_desc( p_head, p_head->cur_index ), already_num * sizeof( buf_desc_s ) );
		free( p_head );
		p_head = p_malloc_head;
		*pp_head = p_head;
	}
	mempcpy( get_buf_desc( p_malloc_head, p_malloc_head->last_index - num + 1 )
		p_desc, num * sizeof( *p_desc ) );
	return 0;
}

int merger_packet( buf_head * pp_buf_head, buf_head p_buf_tail, int is_copy )
{
	int i, need_num;
	packet_buf p_buf;
	buf_desc p_desc;

	if( pp_buf_head == NULL || *pp_buf_head == NULL || p_buf_tail == NULL ){
		return -1;
	}
	need_num = p_buf_tail->last_index - p_buf_tail->cur_index + 1;
	if( _append_buf_to_packet( pp_buf_head, get_buf_desc( p_buf_tail
			, p_buf_tail->cur_index ), need_num,  p_buf_tail->buf_len ) < 0 ){
		return -1;
	}
	if( is_copy ){
		for( i = p_buf_tail->cur_index; i <= p_buf_tail->last_index; i++ ){
			p_desc = get_buf_desc( p_buf_tail, i );
			p_buf = ( packet_buf )p_desc - 1;
			__sync_add_and_fetch( &p_buf->ref_num );
		}
	}else{
		free( p_buf_tail );
	}
	return 0;
}

