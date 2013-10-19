#include<stdio.h>
#include<stdlib.h>
#include<pthread.h>
#include<string.h>
#include"moon_debug.h"
#include"shadow_struct.h"

//版本号从0开始

struct addr_pair_s{
	union{
		unsigned long virtual_addr;
		unsigned long id;
	};
	struct mem_block_s * addr;
};
typedef struct addr_pair_s addr_pair_s;
typedef struct addr_pair_s * addr_pair;
typedef struct addr_pair_s id_pair_s;
typedef struct addr_pair_s * id_pair;

struct need_del_s{  //  need_del_s + void *[]
	unsigned long num;
};
typedef struct need_del_s need_del_s;
typedef struct need_del_s * need_del;

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

struct version_info_s{ // double_list_s + version_info_s 
	unsigned long version;
	unsigned long last_version;
	unsigned long ref_num;
	unsigned long block_num;
	addr_pair order_addr; //按虚拟地址排序的数组
	id_pair order_id; //按id排序的数组
	need_del mem_del;
};
typedef struct version_info_s version_info_s;
typedef struct version_info_s * version_info;

struct mem_block_s{ //mem_block_s + data
	unsigned long id; //内存id，递增的，从1开始。
	unsigned long version;//最初创建时属于那个版本。
	void *virtual_addr;
	unsigned long is_del;
	unsigned long is_root; //0 不是，1是全局变量，2是副本
	unsigned long type_id;//对应的类型的序号
	unsigned long type_num;//元素个数
	unsigned long data_len;//数据快总长度
	char * data;//数据快指针，大多时候紧邻这个结构体一起分配
};
typedef struct mem_block_s mem_block_s;
typedef struct mem_block_s * mem_block;

struct thread_private_s{
	version_info version;
	avl_tree avl; //avl_tree_s + addr_pair_s
};
typedef struct thread_private_s thread_private_s;
typedef struct thread_private_s * thread_private;

//以前的结构太简单，现在要改为可有多个镜像类别，每个类别又可有多个实体。
#define HASH_NUM 67
#define HASH_BASE 61

struct hash_key_s{
	char key[256];
};
typedef struct hash_key_s hash_key_s;
typedef struct hash_key_s * hash_key;

struct shadow_entity_s{
//	char xid[256]; //标识一个实体的
	double_list version_head;//double_list_s + version_info_s
	pthread_mutex_t version_mutex;
	thread_private_s native_versions[ 10 ];//最多保留10个版本
	long base_num;//用来叠加在ersion号上的，防止重复。
	long cur_version;
	//下面这些是以后使用或改变的
	unsigned long ref_num;
	unsigned long status;
};
typedef struct shadow_entity_s shadow_entity_s;
typedef struct shadow_entity_s * shadow_entity;

struct shadow_type_s{
//	char name[256];//标识一个镜像类型的
	double_list init_version; //double_list_s + version_info_s
	struct_all struct_types;
	double_list entity_table[ HASH_NUM ]; //double_list_s + hash_key_s + shadow_entity_s
	shadow_entity cur_entity;
	pthread_mutex_t entity_mutex;
	//下面可添加回调函数
};
typedef struct shadow_type_s shadow_type_s;
typedef struct shadow_type_s * shadow_type;

static double_list shadow_head[ HASH_NUM ] = { NULL };//double_list_s + hash_key_s + shadow_type_s

static unsigned long hash_func( const char * key )
{
	char *tmp;
	unsigned long hash;

	hash = 0;
	for( tmp = key; *tmp != '\0'; tmp++ ){
		hash *= HASH_BASE;
		hash += *tmp;
		hash %= HASH_NUM;
	}
	return hash;
}

void * hash_search( double_list * table, char * key, unsigned long data_len )
{
	double_list dlist;
	hash_key p_key;
	unsigned long index;

	if( table == NULL || key == NULL ){
		return NULL;
	}
	index = hash_func( key );
	dlist = table[ index ];
	for( ; dlist != NULL; dlist = dlist->next ){
		p_key = ( hash_key )( dlist + 1 );
		if( strcmp( p_key->key, key ) == 0 ){
			return p_key + 1;
		}
	}
	if( data_len > 0 ){ //new
		dlist = ( double_list )malloc( sizeof( double_list_s ) + sizeof( hash_key_s ) + data_len );
		if( dlist == NULL ){
			return NULL;
		}   
		p_key = ( hash_key )( dlist + 1 );
		strncpy( p_key->key, key, sizeof( p_key->key ) );
		dlist->prev = NULL;
		dlist->next = table[ index ];
		if( table[ index ] != NULL ){
			table[ index ]->prev = dlist;
		}
		table[ index ] = dlist;
		return p_key + 1;
	}
	return NULL;
}

void avl_print( avl_tree avl )
{
	addr_pair pair;
	if( avl == NULL ){
		return;
	}
	avl_print( avl->left );
	pair = ( addr_pair )( avl + 1 );
	printf( "[ %ld:%d ]", pair->virtual_addr, avl->balance );
	avl_print( avl->right );
	if( avl->parent == NULL ){
		printf( "\n" );
	}
}

void avl_free( avl_tree * pavl )
{
	if( *pavl != NULL ){
		avl_free( &( *pavl )->left );
		avl_free( &( *pavl )->right );
		free( *pavl );
		*pavl = NULL;
	}
}

addr_pair avl_search( avl_tree avl, void * addr )
{
	avl_tree tmp_avl;
	addr_pair pair, last;

	last = NULL;
	tmp_avl = avl;
	while( tmp_avl != NULL ){
		pair = ( addr_pair )( tmp_avl + 1 );
		if( pair->virtual_addr > ( unsigned long )addr ){
			tmp_avl = tmp_avl->left;
		}else if( pair->virtual_addr == ( unsigned long )addr ){
			return pair;
		}else{
			last = pair;
			tmp_avl = tmp_avl->right;
		}
	}
	return last;
}

#define SET_LR( node, side, child_node) {\
	node->side = child_node; \
	if( node->side != NULL ){ \
		node->side->parent = node; \
	} \
}

#define SET_PARENT( node, other_node, pavl ) {\
	node->parent = other_node->parent; \
	if( node->parent == NULL ){ \
		*pavl = node; \
	}else if( node->parent->left == other_node ){ \
		node->parent->left = node; \
	}else{ \
		node->parent->right = node; \
	} \
}

inline avl_tree avl_balance( avl_tree parent_avl, avl_tree * pavl )
{
	avl_tree tmp_avl, axis_avl;

	if( parent_avl->balance == 2 ){//设置left或right后立马设置其parent
		tmp_avl = parent_avl->right;
		if( tmp_avl->balance >= 0 ){
			axis_avl = tmp_avl;
			SET_LR( parent_avl, right, axis_avl->left )
			SET_PARENT( axis_avl, parent_avl, pavl )
			SET_LR( axis_avl, left, parent_avl )

			if( axis_avl->balance == 0 ){
				parent_avl->balance = 1;
				axis_avl->balance = -1;
				return NULL;
			}else{
				axis_avl->balance = 0;
				parent_avl->balance = 0;
			}
		}else{
			axis_avl = tmp_avl->left;

			SET_LR( parent_avl, right, axis_avl->left )
			SET_LR( tmp_avl, left, axis_avl->right )
			SET_PARENT( axis_avl, parent_avl, pavl )
			SET_LR( axis_avl, left, parent_avl )
			SET_LR( axis_avl, right, tmp_avl )

			parent_avl->balance = 0;
			tmp_avl->balance = 0;
			if( axis_avl->balance == 1 ){
				parent_avl->balance = -1;
			}else if( axis_avl->balance == -1 ){
				tmp_avl->balance = 1;
			}
			axis_avl->balance = 0;
		}
	}else{
		tmp_avl = parent_avl->left;
		if( tmp_avl->balance <= 0 ){
			axis_avl = tmp_avl;
			SET_LR( parent_avl, left, axis_avl->right )
			SET_PARENT( axis_avl, parent_avl, pavl )
			SET_LR( axis_avl, right, parent_avl )

			if( axis_avl->balance == 0 ){
				parent_avl->balance = -1;
				axis_avl->balance = 1;
				return NULL;
			}else{
				axis_avl->balance = 0;
				parent_avl->balance = 0;
			}
		}else{
			tmp_avl = parent_avl->left;
			axis_avl = tmp_avl->right;

			SET_LR( parent_avl, left, axis_avl->right )
			SET_LR( tmp_avl, right, axis_avl->left )
			SET_PARENT( axis_avl, parent_avl, pavl )
			SET_LR( axis_avl, left, tmp_avl )
			SET_LR( axis_avl, right, parent_avl )

			parent_avl->balance = 0;
			tmp_avl->balance = 0;
			if( axis_avl->balance == 1 ){
				tmp_avl->balance = -1;
			}else if( axis_avl->balance == -1 ){
				parent_avl->balance = 1;
			}
			axis_avl->balance = 0;
		}
	}

	return axis_avl;
}

addr_pair avl_add( avl_tree * pavl, void * addr )
{
	avl_tree new_node, * tmp_pavl, parent_avl, tmp_avl, axis_avl;
	addr_pair pair;
	int balance;

	if( pavl == NULL ){
		return NULL;
	}
	new_node = calloc( 1, sizeof( avl_tree_s ) + sizeof( addr_pair_s ) );
	if( new_node == NULL ){
		return NULL;
	}
	pair = ( addr_pair )( new_node + 1 );
	pair->virtual_addr = ( unsigned long )addr;

	tmp_pavl = pavl;
	parent_avl = NULL;
	while( *tmp_pavl != NULL ){
		parent_avl = *tmp_pavl;
		pair = ( addr_pair )( *tmp_pavl + 1 );
		if( pair->virtual_addr > ( unsigned long )addr ){
			tmp_pavl = &( *tmp_pavl )->left;
		}else if( pair->virtual_addr == ( unsigned long )addr ){
			free( new_node );
			return pair;
		}else{
			tmp_pavl = &( *tmp_pavl )->right;
		}
	}

	*tmp_pavl = new_node;
	new_node->parent = parent_avl;
	tmp_avl = new_node;
	while( tmp_avl-> parent != NULL ){
		parent_avl = tmp_avl->parent;
		if( parent_avl->left == tmp_avl ){
			balance = -1;
		}else{
			balance = 1;
		}
		parent_avl->balance += balance;
		switch( parent_avl->balance ){
		case 0:
			break;
		case 1:
		case -1:
			tmp_avl = parent_avl;
			continue;
		case 2:
		case -2:
			avl_balance( parent_avl, pavl );
			break;
		default:
			perror( "dont balance" );
		}
		return ( addr_pair )( new_node + 1 );
	}
}

addr_pair_s avl_del( avl_tree * pavl, void * addr )
{
	addr_pair pair, pair2;
	int balance;
	avl_tree tmp_avl, del_avl, parent_avl, child_avl;
	addr_pair_s del_pair = { 0, 0 };

	if( pavl == NULL ){
		return del_pair;
	}
	tmp_avl = *pavl;
	while( tmp_avl != NULL ){
		pair = ( addr_pair )( tmp_avl + 1 );
		if( pair->virtual_addr > ( unsigned long )addr ){
			tmp_avl = tmp_avl->left;
		}else if( pair->virtual_addr == ( unsigned long )addr ){
			del_pair = *pair;
			break;
		}else{
			tmp_avl = tmp_avl->right;
		}
	}
	if( tmp_avl == NULL ){
		return del_pair;
	}

	del_avl = tmp_avl;
	if( tmp_avl->left == NULL && tmp_avl->right == NULL ){//为叶子节点
		goto start_back;//以此节点开始上溯
	}
	if( tmp_avl->balance <= 0 ){
		tmp_avl = tmp_avl->left;
		for( ; tmp_avl->right != NULL; tmp_avl = tmp_avl->right ){
			;
		}
		child_avl = tmp_avl->left;
	}else{
		tmp_avl = tmp_avl->right;
		for( ; tmp_avl->left != NULL; tmp_avl = tmp_avl->left ){
			;
		}
		child_avl = tmp_avl->right;
	}
	pair =( addr_pair )( tmp_avl + 1 );
	pair2 = ( addr_pair )( del_avl + 1 );
	*pair2 = *pair;
	del_avl = tmp_avl;
	if( child_avl != NULL ){
		pair2 = ( addr_pair )( child_avl + 1 );
		*pair = *pair2;
		del_avl = child_avl;
	}

start_back:
	tmp_avl = del_avl;
	while( tmp_avl->parent != NULL ){
		parent_avl = tmp_avl->parent;
		if( parent_avl->left == tmp_avl ){
			balance = 1;
		}else{
			balance = -1;
		}
		parent_avl->balance += balance;
		switch( parent_avl->balance ){
		case 1:
		case -1:
			goto end;
		case 0:
			tmp_avl = tmp_avl->parent;
			break;
		case 2:
		case -2:
			tmp_avl = avl_balance( parent_avl, pavl );
			if( tmp_avl == NULL ){
				goto end;
			}
			break;
		}
	}
end:
	parent_avl = del_avl->parent;
	if( parent_avl != NULL ){
		if( parent_avl->left == del_avl ){
			parent_avl->left = NULL;
		}else{
			parent_avl->right = NULL;
		}
	}else{
		*pavl = NULL;
	}
	free( del_avl );

	return del_pair;
}
/*
void * shadow_runtime( void * p_data, int len, char *opt )
{
	addr_pair search_addr_id( addr_pair in, unsigned long id, unsigned long num );

	thread_private tp_data = NULL;
	addr_pair cur_pair;
	mem_block p_block, copy_block;
	unsigned long offset;

	if( p_data == NULL 
			|| ( opt[0] != 'r' && opt[0] != 'w' && opt[0] != 'c' ) ){
		goto error;
	}
	tp_data = ( thread_private )pthread_getspecific( public_key );
	if( tp_data == NULL ){
		tp_data = ( thread_private )calloc( 1, sizeof( thread_private_s ) );
		if( tp_data == NULL ){
			goto error;
		}
		pthread_setspecific( public_key, tp_data );
	}
	if( tp_data->version == NULL ){
		pthread_mutex_lock( &version_mutex );
		if( version_head != NULL ){
			tp_data->version = ( version_info )( version_head + 1 );
			tp_data->version->ref_num++;
		}
		pthread_mutex_unlock( &version_mutex );
	}
	if( tp_data->version == NULL ){
		goto error;
	}
	cur_pair = avl_search( tp_data->avl, p_data );
	if( cur_pair != NULL ){
		p_block = cur_pair->addr;
		offset = cur_pair->virtual_addr + p_block->data_len;
		if( ( unsigned long )p_data < offset ){
			offset = ( unsigned long )p_data - cur_pair->virtual_addr;
			return p_block->data + offset;
		}
	}
	cur_pair = search_addr_id( tp_data->version->order_addr, ( unsigned long )p_data, tp_data->version->block_num);
	if( cur_pair == NULL && opt[ 0 ] == 'c' ){
		return p_data;
	}
	if( cur_pair != NULL ){
		p_block = cur_pair->addr;
		offset = cur_pair->virtual_addr + p_block->data_len;
		if( ( unsigned long )p_data < offset ){
			offset = ( unsigned long )p_data - cur_pair->virtual_addr;
			switch( opt[0] ){
			case 'c':
			case 'r':
				return p_block->data + offset;
			case 'w':
				copy_block = ( mem_block )malloc( sizeof( mem_block_s ) + p_block->data_len );
				if( copy_block == NULL ){
					goto error;
				}
				if( p_block->data == ( char * )( p_block + 1 ) ){
					memcpy( copy_block, p_block, sizeof( mem_block_s ) + p_block->data_len );
				}else{
					memcpy( copy_block, p_block, sizeof( *copy_block ) );
					memcpy( copy_block + 1, p_block->data, p_block->data_len );
				}
				cur_pair = avl_add( &tp_data->avl, copy_block->virtual_addr );
				if( cur_pair == NULL ){
					goto error;
				}
				cur_pair->addr = copy_block;
				return copy_block->data + offset;
			}
		}
	}

error:
	printf( "%p:%s\n", p_data, opt );
	return p_data;
}
*/
void * shadow_runtime( void * p_data, int len, char *opt )
{

}
void *shadow_new( char * type, int num )
{
	printf( "shadow_new:%d\n", num );
	return malloc( num );
}

void shadow_del( void *addr )
{
	printf( "shadow_del:%p\n", addr );
	free( addr );
}

void qsort_addr_pair( addr_pair out, unsigned long num )
{
	addr_pair_s cmp;
	unsigned long save_next, cmp_next;
	if( num <= 1){
		return;
	}

	cmp = out[ num >> 1 ];
	out[ num >> 1 ] = out[ 0 ];
	for( cmp_next = 1, save_next = 0; cmp_next < num; cmp_next++ ){
		if( out[ cmp_next ].virtual_addr < cmp.virtual_addr ){
			out[ save_next++ ] = out[ cmp_next ];
			out[ cmp_next ] = out[ save_next ];
		}
	}
	out[ save_next ] = cmp;
	qsort_addr_pair( out, save_next );
	qsort_addr_pair( out + save_next + 1, num - 1 - save_next );
}

addr_pair sort_addr_id( id_pair in_id_pair, unsigned long  num )
{
	addr_pair out_addr_pair = NULL;
	unsigned long i;

	if( num ==0 ){
		return NULL;
	}
	out_addr_pair = malloc( sizeof( addr_pair_s ) * num );
	if( out_addr_pair == NULL ){
		return NULL;
	}
	for( i = 0; i < num; i++ ){
		out_addr_pair[ i ].addr = in_id_pair[ i ].addr;
		out_addr_pair[ i ].virtual_addr = ( unsigned long )( ( mem_block )out_addr_pair[ i ].addr )->virtual_addr;
	}
	qsort_addr_pair( out_addr_pair, num );
	return out_addr_pair;
}

//找到小于等于目标地址的最接近的块
addr_pair search_addr_id( addr_pair in, unsigned long id, unsigned long num )
{
	unsigned long left, right_next, mid;
	addr_pair last = NULL;

	if( num == 0 ){
		return 0;
	}
	if( in[ 0 ].id > id ){//必须要处理长度为一的特殊情况。
		return NULL;
	}else if( in[ 0 ].id == id ){
		return &in[ 0 ];
	}else{
		last = &in[ 0 ];
	}

	for( left = 1, right_next = num; right_next > left; ){
		mid = ( left >> 1 ) + ( right_next >> 1 );
		if( in[ mid ].id == id ){
			return &in[ mid ];
		}else if( in[ mid ].id < id ){
			last = &in[ mid ];
			left = mid + 1;
		}else{
			right_next = mid;
		}
	}
	return last;
}
