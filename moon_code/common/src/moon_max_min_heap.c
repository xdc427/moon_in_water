#include<stdlib.h>
#include<stdio.h>
#include"moon_common.h"
#include"moon_debug.h"
#include"moon_max_min_heap.h"

typedef struct max_min_heap_s{
	int is_min;
	int elem_size;
	unsigned char * buf;
	//cur_buf_len <= capacity - 1;最后一块作为交换区
	int cur_buf_len;
	int capacity;
	void * p_last_elem;
	// > 0:p_data1 > p_data2, = 0:p_data1 == p_data2, < 0:p_data1 < p_data2
	int ( * compare )( void * p_data1, void * p_data2 );
} max_min_heap_s;

#define CAPACITY_DEFAULT 1023
#define IS_SWAP( p_heap, cmp )\
 ( ( ( p_heap )->is_min && ( cmp ) < 0 ) || ( !( p_heap )->is_min && ( cmp ) > 0 ) )

max_min_heap heap_init( int is_min, int elem_size, int capacity, int ( * compare )( void *, void * ) )
{
	max_min_heap p_heap;
	
	if( elem_size <= 0 || compare == NULL ){
		MOON_PRINT_MAN( ERROR, " input paraments error!" );
		return NULL;
	}
	if( capacity <= 0 ){
		capacity = CAPACITY_DEFAULT;
	}
	capacity++;
	p_heap = ( max_min_heap )malloc( sizeof( *p_heap ) );
	if( p_heap == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		return NULL;
	}
	p_heap->buf = malloc( capacity * elem_size );
	if( p_heap == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc buf error" );
		free( p_heap );
		return NULL;
	}
	p_heap->cur_buf_len = 0;
	p_heap->p_last_elem = p_heap->buf + ( capacity - 1 ) * elem_size;
	p_heap->is_min = is_min;
	p_heap->elem_size = elem_size;
	p_heap->capacity = capacity;
	p_heap->compare = compare;
	return p_heap;
}

void heap_free( max_min_heap p_heap )
{
	if( p_heap != NULL ){
		IF_FREE( p_heap->buf );
		free( p_heap );
	}
}

static inline void swap( void * p1, void * p2, void * swap_buf, int size )
{
	memcpy( swap_buf, p1, size );
	memcpy( p1, p2, size );
	memcpy( p2, swap_buf, size );
}

static void inline heap_up( max_min_heap p_heap, int index )
{
	int parent_index;
	int cmp;
	void * p_child, * p_parent;

	parent_index = ( ( index + 1 ) >> 2 ) - 1;
	while( parent_index >= 0 ){
		p_child = p_heap->buf + p_heap->elem_size * index;
		p_parent = p_heap->buf + p_heap->elem_size * parent_index;
		cmp = p_heap->compare( p_child, p_parent );
		if( IS_SWAP( p_heap, cmp ) ){
			swap( p_child, p_parent, p_heap->p_last_elem, p_heap->elem_size );
		}else{
			break;
		}
		index = parent_index;
		parent_index = ( ( index + 1 ) >> 2 ) - 1;
	}
}

static void inline heap_down( max_min_heap p_heap, int index ) 
{
	int i, child_index, min_index;
	int cmp;
	void * p_child,* p_min;

	child_index = ( index << 1 ) + 1; 
	while( child_index < p_heap->cur_buf_len ){
		min_index = index;
		p_min = p_heap->buf + p_heap->elem_size * index;
		for( i = 0; i < 2 && p_heap->cur_buf_len > child_index + i; i++ ){
			p_child = p_heap->buf + p_heap->elem_size * ( child_index + i );
			cmp = p_heap->compare( p_min, p_child );
			if( IS_SWAP( p_heap, cmp ) ){
				min_index = child_index + i;
				p_min = p_child;
			}
		}
		if( index == min_index ){
			break;
		}
		index = min_index;
		child_index = ( index << 1 ) + 1;
	}
}

int heap_push( max_min_heap p_heap, void * p_data )
{
	unsigned char * tmp_buf;

	if( p_data == NULL ){
		return -1;
	}
	if( p_heap->capacity - 1 <= p_heap->cur_buf_len ){
		tmp_buf = realloc( p_heap->buf, p_heap->capacity * p_heap->elem_size * 2 );
		if( tmp_buf == NULL ){
			return -1;
		}
		p_heap->buf = tmp_buf;
		p_heap->capacity *= 2;
		p_heap->p_last_elem = p_heap->buf + ( p_heap->capacity - 1 ) * p_heap->elem_size;
	}
	memcpy( p_heap->buf + p_heap->cur_buf_len * p_heap->elem_size, p_data, p_heap->elem_size );
	p_heap->cur_buf_len++;
	heap_up( p_heap, p_heap->cur_buf_len - 1 );
	return 0;
}

int heap_pop( max_min_heap p_heap, void * p_data )
{
	if( p_heap->cur_buf_len <= 0 ){
		return -1;
	}
	memcpy( p_data, p_heap->buf, p_heap->elem_size );
	p_heap->cur_buf_len--;
	if( p_heap->cur_buf_len > 0 ){
		memcpy( p_heap->buf, p_heap->buf + p_heap->cur_buf_len * p_heap->elem_size, p_heap->elem_size );
		heap_down( p_heap, 0 );
	}
	return 0;
}

int heap_length( max_min_heap p_heap )
{
	return p_heap->cur_buf_len;
}

int heap_top( max_min_heap p_heap, void * p_data )
{
	if( p_heap->cur_buf_len <= 0 ){
		return -1;
	}
	memcpy( p_data, p_heap->buf, p_heap->elem_size );
	return 0;
}

