#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<stdint.h>
#include"moon_common.h"

#define BLOCK_TO_BUF( p_block ) ( ( rs_buf )( ( char * )( p_block ) - ( p_block )->offset ) )
 
//now don,t > 8MB
//BUF_BASE_SIZE -> ( BUF_BASE_SIZE << MAX_BUF_SHIFT )
enum{ 
	BUF_RESERVED = sizeof( rs_buf_s ) + sizeof( rs_block_s ),
	BUF_BASE_SIZE = 1024，
	BUF_BASE_ACTUAL_SIZE = BUF_BASE_SIZE - BUF_RESERVED,
	BUF_ALIGN = sizeof( long )
};

enum{
	IS_READY = 0x1,
	IS_HEAD = 0x2,
	IS_TAIL = 0x4
};

//rs_buf_s + payload
typedef struct rs_buf_s{
	uint32_t ref_num;
	uint32_t len;
} rs_buf_s;
typedef rs_buf_s * rs_buf;

typedef struct rs_block_s{
	uint64_t len:24;
	uint64_t offset:24;
	uint64_t flags:16;
	struct rs_block_s * p_next;
} rs_block_s;
typedef rs_block_s * rs_block;

typedef struct{
	int total_len;
	rs_block p_head;
	rs_block p_tail;
	rs_block p_last_packet_head;
	rs_block last_packet_len;
	rs_block p_common_buf;
	int common_buf_left;
	int head_offset;//head rs block offset
	int padding_len;
} rs_buf_hub_s;
typedef rs_buf_hub_s * rs_buf_hub;

void rs_buf_ref_inc( rs_buf p_buf )
{
	GC_REF_INC( p_buf );
}

int rs_buf_ref_dec( rs_buf p_buf )
{
	int ref_num, free_len;

	free_len = 0;
	ref_num = GC_REF_DEC( p_buf );
	if( ref_num == 0 ){
		free_len = p_buf->len;
		free( p_buf );
	}
	return free_len;
}

void rs_block_ref_inc( rs_block p_block )
{
	for( ; p_block != NULL; p_block = p_block->next ){
		rs_buf_ref_inc( BLOCK_TO_BUF( p_block ) );
	}
}

int rs_block_ref_dec( rs_block p_block )
{
	int len = 0;
	rs_block p_tmp;

	while( p_block != NULL ){
		p_tmp = p_block;
		p_block = p_block->next;
		len += rs_buf_ref_dec( BLOCK_TO_BUF( p_tmp ) );
	}
	return len;
}

int get_rs_block_size( rs_block p_block )
{
	int size = 0;

	for( ;p_block != NULL; p_block = p_block->next ){
		size += BLOCK_TO_BUF( p_block )->len;
	}
	return size;
}

rs_block get_rs_block( rs_buf_hub p_hub, int len, int * p_num, int is_continue, rs_block * pp_buf_free )
{
	int tmp, fetch_num;
	rs_block p_block, p_free, p_tmp;
	
	p_free = *pp_buf_free;
	tmp = p_hub->common_buf_left;
	p_block = p_hub->p_common_buf;
	while( p_block != NULL ){
		fetch_num = tmp / len > *p_num ? *p_num : tmp / len;
		if( fetch_num > 0 ){
			p_block->len += fetch_num * len;
			tmp -= fetch_num * len;
			break;
		}else{
			p_tmp = p_block;
			p_block = p_block->next;
			tmp = 0;
			if( p_block != NULL ){
				tmp = p_block->len;
				p_block->len = 0;
			}
			if( is_continue == 0 ){
				p_tmp->next = p_free;
				p_free = p_tmp;
			}
			is_continue = 0;
		}
	}
	p_hub->common_buf_left = tmp;
	p_hub->p_common_buf = p_block;
	*pp_buf_free = p_free;
	*p_num -= fetch_num;
	return p_block;
}

void move_common_buf( rs_buf_hub p_hub, int min_left )
{
	int left, tmp;
	rs_block p_common, p_block;
	char * p_next;

	left = p_hub->common_buf_left;
	p_common = p_hub->p_common_buf;
	p_next = ( char * )( p_common + 1 ) + p_common->len;
	p_block = ( rs_block )ROUND_UP( ( uintptr_t )p_next, BUF_ALIGN );
	tmp = POINT_OFFSET( p_block, p_next ) + sizeof( *p_block );
	if( tmp + min_left <= left ){
		p_block->offset = p_common->offset + POINT_OFFSET( p_block, p_common );
		p_block->len = 0;
		p_block->flags = 0;
		p_block->next = NULL;
		p_hub->p_common_buf = p_block;
		p_hub->common_buf_left = left - tmp;
		rs_block_ref_inc( p_block );
	}else{
		p_block = p_block->next;
		p_hub->p_common_buf = p_block;
		p_hub->common_buf_left = 0;
		if( p_block != NULL ){
			p_hub->common_buf_left = p_block->len;
			p_block->len = 0;
		}
	}
}

//因为block一般就是一到两块，所以遍历没问题
rs_block get_from_head( rs_buf_hub p_hub )
{
	rs_block p_block, p_block_head;
	int len;

	len = 0;
	p_block_head = NULL;
	p_block = p_hub->p_head;
	if( p_block != NULL 
		&& ( p_block->flags & ( IS_READY | IS_HEAD ) ) == ( IS_HEAD | IS_HEAD ) ){
		p_block_head = p_block;
		for( ; ( p_block->flags & IS_TAIL ) == 0; p_block = p_block->next ){
			len += p_block->len;
		}
		len += p_block->len;
		p_hub->p_head = p_block->next;
		p_block->next = NULL;
		if( p_hub->p_head == NULL ){
			p_hub->p_tail = NULL;
		}
		p_hub->total_len -= len;
	}
	return p_block_head;
}

rs_block get_from_head_len( rs_buf_hub p_hub, int len, int * p_offset )
{
	rs_block p_block_head, p_block;
	int cur_len, offset, tmp;

	cur_len = 0;
	p_block = p_hub->p_head;
	offset = p_hub->head_offset;
	*p_offset = offset;
	if( p_block != NULL 
		&& ( p_block->flags & ( IS_READY | IS_HEAD ) ) == ( IS_HEAD | IS_READY ) ){
		p_block_head = p_block;
		for( ; ; ){
			tmp = p_block->len - offset;
			if( cur_len + tmp > len ){
				p_hub->head_offset = len - cur_len + offset;
				p_hub->p_head = p_block;
				rs_buf_ref_inc( BLOCK_TO_BUF( p_block ) );
				p_block->flags |= IS_HEAD | IS_READY;
				cur_len = len;
				break;
			}else( ( p_block->flags & IS_TAIL ) != 0 ){
				p_hub->head_offset = 0;
				p_hub->p_head = p_block->next;
				p_block->next = NULL;
				cur_len += tmp;
				break;
			}else if( cur_len + tmp == len ){
				p_hub->head_offset = 0;
				p_hub->p_head = p_block->next;
				p_block->next = NULL;
				p_hub->p_head->flags |= IS_HEAD | IS_READY;
				cur_len += tmp;
				break;
			}
			offset = 0;
			cur_len += tmp;
			p_block = p_block->next;
		}
		p_hub->total_len -= cur_len;
		if( p_block->head == NULL ){
			p_block->p_tail = NULL;
		}
	}
	return p_block_head;
}

void append_to_tail( rs_buf_hub p_hub, rs_block p_block )
{
	int len;
	rs_block p_tail;

	len = 0;
	for( p_tail = p_block; p_tail->next != NULL; p_tail = p_tail->next ){
		len += p_tail->len;
	}
	len += p_tail->len;
	if( p_hub->p_tail != NULL ){
		p_hub->p_tail->next = p_block;
	}else{
		p_hub->p_head = p_block;
	}
	p_hub->p_tail = p_tail;
	p_hub->total_len += len;
}

