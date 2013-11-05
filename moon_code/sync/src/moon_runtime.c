#include<stdio.h>
#include<stdlib.h>
#include<pthread.h>
#include<string.h>
#include"zlib.h"
#include"moon_runtime.h"
#include"moon_debug.h"
#include"shadow_struct.h"
#include<openssl/md5.h>
#include"moon_packet.h"
#include"moon_common.h"

//版本号从0开始

struct addr_pair_s{
	union{
		unsigned long long virtual_addr;
		unsigned long long id;
	};
	union{
		struct mem_block_s * addr;
		unsigned long num_data;
	};
};
typedef struct addr_pair_s addr_pair_s;
typedef struct addr_pair_s * addr_pair;
typedef struct addr_pair_s id_pair_s;
typedef struct addr_pair_s * id_pair;

struct version_info_s{ // double_list_s + version_info_s 
	unsigned long version;
	unsigned long last_version;
	unsigned long ref_num;
	int  block_num;
	int status;//0:正常，-1过时。
	char md5_sum[ 16 ];//所有块按id排序后所有块md5的md5.
	addr_pair order_addr; //按虚拟地址排序的数组
	id_pair order_id; //按id排序的数组
	int  del_num;
	struct mem_block_s ** del_blocks ;
	struct shadow_entity_s * entity;
};
typedef struct version_info_s version_info_s;
typedef struct version_info_s * version_info;

struct mem_block_s{ //mem_block_s + data
	unsigned long long id; //内存id，递增的，从1开始。
	unsigned long long tmp_id;//只在生成一个提交时临时保存新分配的id
	unsigned long version;//最初创建时属于那个版本。
	void *  virtual_addr;
	int is_del;
	int is_root; //0 不是，1是全局变量，2是副本
	int type_id;//对应的类型的序号
	int type_num;//元素个数
	int data_len;//数据快总长度
	char md5_sum[ 16 ];//按主体的结构指针在前数据在后计算的md5
	char * data;//数据快指针，大多时候紧邻这个结构体一起分配
};
typedef struct mem_block_s mem_block_s;
typedef struct mem_block_s * mem_block;

struct thread_private_s{
	char type[ 256 ];
	char xid[ 256 ];
	version_info version;
	pthread_mutex_t avl_mutex;
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

struct shadow_entity_s{//list_s + shadow_entity_s
//	char xid[256]; //标识一个实体的
	char md5_sum[ 16 ];//md5;type + xid
	struct shadow_type_s * shadow_type;
	double_list version_head;//double_list_s + version_info_s
	pthread_mutex_t version_mutex;
	//下面这些是以后使用或改变的
	struct_all remote_all;
	unsigned long ref_num;
	int  status;
};
typedef struct shadow_entity_s shadow_entity_s;
typedef struct shadow_entity_s * shadow_entity;

struct shadow_type_s{
//	char name[256];//标识一个镜像类型的
	double_list init_version; //double_list_s + version_info_s
	struct_all struct_types;
	double_list entity_table[ HASH_NUM ]; //double_list_s + hash_key_s + shadow_entity_s
	pthread_mutex_t entity_mutex;
	//下面可添加回调函数
};
typedef struct shadow_type_s shadow_type_s;
typedef struct shadow_type_s * shadow_type;

//数据包结构
struct packet_head_s{
	char version[ 2 ]; // 2Byte:MX,X为版本，现在为0
	uint16_t cmd;//2Byte:指令
	uint32_t len; //4Byte:数据包总长度
	unsigned char md5_sum[ 16 ];//16B:shadow类型与实体名合在一起的md5
}__attribute__((packed));
typedef struct packet_head_s packet_head_s;
typedef struct packet_head_s * packet_head;

enum{
	HAS_DELS = 0x1,
	HAS_WRITES = 0x2,
	HAS_NEWS = 0x4
};

struct packet_update_head_s{
	unsigned char from_md5[ 16 ];//修改的版本的md5
	unsigned char to_md5[ 16 ];//修改后版本的md5
	uint16_t info_bits;//指示del，write，new是否存在
}__attribute__((packed));
typedef struct packet_update_head_s packet_update_head_s;
typedef struct packet_update_head_s * packet_update_head;

typedef struct packet_change_s{ 
	int num;
	int len;
	union{
		addr_pair pairs;
		unsigned long long * ids;
	};
	addr_pair prev_pairs;
} packet_change_s;
typedef packet_change_s * packet_change;

enum{
	IS_XOR = 0x1,
	IS_COMPRESS = 0x2
};

typedef struct packet_data_head_s{
	uint32_t original_len;
	uint16_t mask;
}__attribute__((packed)) packet_data_head_s;
typedef packet_data_head_s * packet_data_head;

struct packet_data_s{
	packet_data_head_s head;
	int len;
	char * data;
};
typedef struct packet_data_s packet_data_s;
typedef struct packet_data_s * packet_data;

struct packet_id_offset_s{
	uint64_t id;
	uint32_t offset;
}__attribute__((packed));
typedef struct packet_id_offset_s packet_id_offset_s;
typedef struct packet_id_offset_s * packet_id_offset;

typedef struct packet_s{
	packet_head_s head;
	packet_update_head_s update_head;
	packet_change_s changes[ 3 ];//del,write,new
	packet_data_s data;
	//辅助数据
	version_info version;
	struct_all from_all;
	struct_all to_all;
	unsigned long long write_id_start;
	unsigned long long new_id_start;
	int write_block_size;
	int new_block_size;
	int cur_new_index;
	int cur_write_index;
	int cur_del_index;
} packet_s;
typedef packet_s * packet;

//packet end

static double_list shadow_head[ HASH_NUM ] = { NULL };//double_list_s + hash_key_s + shadow_type_s
static pthread_key_t shadow_key; 

inline void shadow_env_set( void * tp )
{
	pthread_setspecific( shadow_key, tp );
}

inline void shadow_env_init()
{
	pthread_key_create( &shadow_key, NULL );
}

inline void * shadow_env_get()
{
	return pthread_getspecific( shadow_key );
}

static unsigned long hash_func( const char * key )
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

void * hash_search( double_list * table, char * key, int data_len )
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

int avl_traver_first( avl_tree avl, int ( *func )( addr_pair, void * ), void * para )
{
	addr_pair pair;

	if( avl == NULL ){
		return 0;
	}
	if( avl_traver_first( avl->left, func, para ) < 0 ){
		return -1;
	}
	pair = ( addr_pair )( avl + 1 ); 
	if( func( pair, para ) < 0 ){
		return -1;
	}
	if( avl_traver_first( avl->right, func, para ) < 0 ){
		return -1;
	}
	return 0;
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
		if( pair->virtual_addr > ( uintptr_t )addr ){
			tmp_avl = tmp_avl->left;
		}else if( pair->virtual_addr == ( uintptr_t )addr ){
			return pair;
		}else{
			last = pair;
			tmp_avl = tmp_avl->right;
		}
	}
	return last;
}

#define SET_LR( node, side, child_node) do{\
	( node )->side = ( child_node ); \
	if( ( node )->side != NULL ){ \
		( node )->side->parent = ( node ); \
	} \
}while( 0 )

#define SET_PARENT( node, other_node, pavl ) do{\
	( node )->parent = ( other_node )->parent; \
	if( ( node )->parent == NULL ){ \
		*( pavl ) = ( node ); \
	}else if( ( node )->parent->left == ( other_node ) ){ \
		( node )->parent->left = ( node ); \
	}else{ \
		( node )->parent->right = ( node ); \
	} \
}while( 0 )

inline avl_tree avl_balance( avl_tree parent_avl, avl_tree * pavl )
{
	avl_tree tmp_avl, axis_avl;

	if( parent_avl->balance == 2 ){//设置left或right后立马设置其parent
		tmp_avl = parent_avl->right;
		if( tmp_avl->balance >= 0 ){
			axis_avl = tmp_avl;
			SET_LR( parent_avl, right, axis_avl->left );
			SET_PARENT( axis_avl, parent_avl, pavl );
			SET_LR( axis_avl, left, parent_avl );

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

			SET_LR( parent_avl, right, axis_avl->left );
			SET_LR( tmp_avl, left, axis_avl->right );
			SET_PARENT( axis_avl, parent_avl, pavl );
			SET_LR( axis_avl, left, parent_avl );
			SET_LR( axis_avl, right, tmp_avl );

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
			SET_LR( parent_avl, left, axis_avl->right );
			SET_PARENT( axis_avl, parent_avl, pavl );
			SET_LR( axis_avl, right, parent_avl );

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

			SET_LR( parent_avl, left, axis_avl->right );
			SET_LR( tmp_avl, right, axis_avl->left );
			SET_PARENT( axis_avl, parent_avl, pavl );
			SET_LR( axis_avl, left, tmp_avl );
			SET_LR( axis_avl, right, parent_avl );

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
	pair->virtual_addr = ( uintptr_t )addr;

	tmp_pavl = pavl;
	parent_avl = NULL;
	while( *tmp_pavl != NULL ){
		parent_avl = *tmp_pavl;
		pair = ( addr_pair )( *tmp_pavl + 1 );
		if( pair->virtual_addr > ( uintptr_t )addr ){
			tmp_pavl = &( *tmp_pavl )->left;
		}else if( pair->virtual_addr == ( uintptr_t )addr ){
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
			MOON_PRINT_MAN( ERROR, "dont balance" );
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
		if( pair->virtual_addr > ( uintptr_t )addr ){
			tmp_avl = tmp_avl->left;
		}else if( pair->virtual_addr == ( uintptr_t )addr ){
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

void * shadow_runtime( char * type, void * p_data, int len, char * opt )
{
	addr_pair search_addr_id( addr_pair in, unsigned long long id, unsigned long num );

	thread_private tp_data = NULL;
	addr_pair cur_pair;
	mem_block p_block, copy_block;
	int offset;

	if( type == NULL || p_data == NULL 
			|| ( opt[0] != 'r' && opt[0] != 'w' && opt[0] != 'c' ) ){
		MOON_PRINT_MAN( ERROR, "input paraments error!" );
		return p_data;
	}
	tp_data = ( thread_private )shadow_env_get();
	if( tp_data == NULL || tp_data->version == NULL ){
		MOON_PRINT_MAN( ERROR, "shadow env error!" );
		return p_data;
	}
	pthread_mutex_lock( &tp_data->avl_mutex );
	cur_pair = avl_search( tp_data->avl, p_data );
	pthread_mutex_unlock( &tp_data->avl_mutex );
	if( cur_pair != NULL ){
		p_block = cur_pair->addr;
		offset = cur_pair->virtual_addr + p_block->data_len;
		if( ( uintptr_t )p_data < offset ){
			offset = ( uintptr_t )p_data - cur_pair->virtual_addr;
			return p_block->data + offset;
		}
	}
	cur_pair = search_addr_id( tp_data->version->order_addr, ( uintptr_t )p_data, tp_data->version->block_num);
	if( cur_pair == NULL && opt[ 0 ] == 'c' ){
		return p_data;
	}
	if( cur_pair != NULL ){
		p_block = cur_pair->addr;
		offset = cur_pair->virtual_addr + p_block->data_len;
		if( ( uintptr_t )p_data < offset ){
			offset = ( uintptr_t )p_data - cur_pair->virtual_addr;
			switch( opt[0] ){
			case 'c':
			case 'r':
				return p_block->data + offset;
			case 'w':
				copy_block = ( mem_block )malloc( sizeof( mem_block_s ) + p_block->data_len );
				if( copy_block == NULL ){
					MOON_PRINT_MAN( ERROR, "malloc block error!" );
					return p_data;
				}
				if( p_block->data == ( char * )( p_block + 1 ) ){
					memcpy( copy_block, p_block, sizeof( mem_block_s ) + p_block->data_len );
				}else{
					memcpy( copy_block, p_block, sizeof( *copy_block ) );
					memcpy( copy_block + 1, p_block->data, p_block->data_len );
				}
				pthread_mutex_lock( &tp_data->avl_mutex );
				cur_pair = avl_add( &tp_data->avl, copy_block->virtual_addr );
				if( cur_pair == NULL ){
					pthread_mutex_unlock( &tp_data->avl_mutex );
					MOON_PRINT_MAN( ERROR, "insert avl tree error!" );
					return p_data;
				}
				if( cur_pair->addr != NULL ){//同时有其他线程在写
					pthread_mutex_unlock( &tp_data->avl_mutex );
					free( copy_block );
					p_block = cur_pair->addr;
					return p_block->data + offset;
				}
				cur_pair->addr = copy_block;
				pthread_mutex_unlock( &tp_data->avl_mutex );
				return copy_block->data + offset;
			}
		}else{
			MOON_PRINT_MAN( ERROR, "address is error " );
			return p_data;
		}
	}else{
		MOON_PRINT_MAN( ERROR, "address is error!" );
		return p_data;
	}
}

void *shadow_new( char * type, char * struct_type, int num , int size )
{
	shadow_type p_shadow;
	int type_index;
	mem_block p_block;
	thread_private tp;
	addr_pair pair;

	if( type == NULL || struct_type == NULL || num <= 0 || size <= 0 ){
		MOON_PRINT_MAN( ERROR, "input paraments error!" );
		return NULL;
	}
	tp = shadow_env_get();
	if( tp == NULL || tp->version == NULL ){
		MOON_PRINT_MAN( ERROR, " shadow env error!" );
		return NULL;
	}
	p_shadow = hash_search( shadow_head, type, 0 );
	if( p_shadow == NULL || p_shadow->struct_types == NULL ){
		MOON_PRINT_MAN( ERROR, "shadow type error:%s!", type );
		return NULL;
	}
	type_index = find_type( p_shadow->struct_types, struct_type );
	if( type_index <= 0 ){
		MOON_PRINT_MAN( ERROR, "can't find such type:%s!", struct_type );
		return NULL;
	}
	p_block = ( mem_block )calloc( 1, size + sizeof( mem_block_s ) );
	p_block->virtual_addr = p_block + 1;
	p_block->type_id = type_index;
	p_block->type_num = num;
	p_block->data_len = size;
	p_block->data = ( char * )p_block + 1;

	pthread_mutex_lock( &tp->avl_mutex );
	pair = avl_add( &tp->avl, p_block->virtual_addr );
	pthread_mutex_unlock( &tp->avl_mutex );
	if( pair == NULL ){
		MOON_PRINT_MAN( ERROR, "insert avl error!" );
		return NULL;
	}
	pair->addr = p_block;
	return p_block->data;
}

void shadow_del( char * type, void *addr )
{
	addr_pair search_addr_id( addr_pair in, unsigned long long id, unsigned long num );

	thread_private tp;
	addr_pair_s pair;
	addr_pair p_pair;
	mem_block p_block;

	if( addr == NULL ){
		MOON_PRINT_MAN( ERROR, "input paraments error!" );
		return;
	}
	tp = shadow_env_get();
	if( tp == NULL || tp->version == NULL ){
		MOON_PRINT_MAN( ERROR, "shadow env error!" );
		return;
	}
	pthread_mutex_lock( &tp->avl_mutex );
	p_pair = avl_search( tp->avl, addr);
	pthread_mutex_unlock( &tp->avl_mutex);
	if( p_pair != NULL && p_pair->virtual_addr == ( uintptr_t )addr ){
		if( p_pair->addr->id == 0 ){//新建的
			pthread_mutex_lock( &tp->avl_mutex );
			pair = avl_del( &tp->avl, addr );
			pthread_mutex_unlock( &tp->avl_mutex );
			free( pair.addr );
		}else{
			p_pair->addr->is_del = 1;
		}
		return;
	}
	p_pair = search_addr_id( tp->version->order_addr, ( uintptr_t )addr, tp->version->block_num );
	if( p_pair == NULL || p_pair->virtual_addr != ( uintptr_t )addr ){
		MOON_PRINT_MAN( ERROR, "address is error!" );
		return;
	}
	p_block = ( mem_block )malloc( sizeof( mem_block_s ) );
	if( p_block == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		return;
	}
	memcpy( p_block, p_pair->addr, sizeof( *p_block ) );
	p_block->is_del = 1;
	pthread_mutex_lock( &tp->avl_mutex );
	p_pair = avl_add( &tp->avl, addr );
	pthread_mutex_unlock( &tp->avl_mutex );
	if( p_pair == NULL ){
		MOON_PRINT_MAN( ERROR, "insert avl error!" );
		return;
	}
	p_pair->addr = p_block;
	return;
}

void * shadow_env_new( char *type, char * xid, unsigned long ver_num )
{
	shadow_type p_type;
	shadow_entity p_entity;
	version_info p_version;
	double_list p_dlist;
	thread_private tp;

	if( type == NULL || xid == NULL ){
		MOON_PRINT_MAN( ERROR, "input paraments error!" );
		return NULL;
	}
	tp = ( thread_private )malloc( sizeof( *tp ) );
	if( tp == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		return NULL;
	}
	p_type = ( shadow_type )hash_search( shadow_head, type, 0 );
	if( p_type == NULL ){
		MOON_PRINT_MAN( ERROR, "no such shadow type:%s", type );
		goto error;
	}
	pthread_mutex_lock( &p_type->entity_mutex );
	p_entity = hash_search( p_type->entity_table, xid, 0 );
	if( p_entity == NULL ){
		MOON_PRINT_MAN( ERROR, "no such entity:%s",  xid );
		pthread_mutex_unlock( &p_type->entity_mutex );
		goto error;
	}
	p_entity->ref_num ++;
	pthread_mutex_unlock( &p_type->entity_mutex );

	p_version = NULL;
	pthread_mutex_lock( &p_entity->version_mutex );
	p_dlist = p_entity->version_head;
	if( p_dlist != NULL && ver_num == NEWEST_VERSION ){
		p_version = ( version_info )( p_dlist + 1 );
	}else{
		for( ; p_dlist != NULL; p_dlist = p_dlist->next ){
			p_version = ( version_info )( p_dlist + 1 );
			if( p_version->version != ver_num ){
				p_version = NULL;
			}
		}
	}
	if( p_version != NULL ){
		p_version->ref_num++;
	}
	pthread_mutex_unlock( &p_entity->version_mutex );

	if( p_version == NULL ){
		MOON_PRINT_MAN( ERROR, "can't find such version!" );
//		check_del_entity( p_entity );
		goto error;
	}
	strncpy( tp->type, type, sizeof( tp->type ) );
	strncpy( tp->xid, xid, sizeof( tp->xid ) );
	tp->version = p_version;
	tp->avl = NULL;
	pthread_mutex_init( &tp->avl_mutex, NULL );
	return tp;

error:
	free( tp );
	return NULL;
}

int shadow_commit( )
{
	
}

void qsort_addr_pair( addr_pair out, int num )
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

addr_pair sort_addr_id( id_pair in_id_pair, int num )
{
	addr_pair out_addr_pair = NULL;
	unsigned long i;

	if( num <= 0 ){
		return NULL;
	}
	out_addr_pair = malloc( sizeof( addr_pair_s ) * num );
	if( out_addr_pair == NULL ){
		return NULL;
	}
	for( i = 0; i < num; i++ ){
		out_addr_pair[ i ].addr = in_id_pair[ i ].addr;
		out_addr_pair[ i ].virtual_addr = ( uintptr_t )( ( mem_block )out_addr_pair[ i ].addr )->virtual_addr;
	}
	qsort_addr_pair( out_addr_pair, num );
	return out_addr_pair;
}

//找到小于等于目标地址的最接近的块
addr_pair search_addr_id( addr_pair in, unsigned long long id, unsigned long num )
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

static inline int get_virtual_address_info( mem_block p_block, void *virtual_addr, unsigned * p_offset, void ** p_real_addr )
{
	int offset;

	offset = ( uintptr_t )p_block->virtual_addr + p_block->data_len - ( uintptr_t )virtual_addr;
	if( offset <= 0 ){
		return -1;
	}else{
		if( p_offset != NULL ){
			*p_offset = offset;
		}
		if( p_real_addr != NULL ){ 
			*p_real_addr = p_block->data + offset;
		}
		return 0;
	}
}

#define _BLOCK_XOR_TYPE( dst, src, len1, len2, type ) do{\
	int _i;\
\
	for( _i = 0; _i < ( len1 ); _i++ ){\
		*( ( type * )( dst ) ) ^= *( ( type * )( src ) );\
		dst = ( void * )( ( type * )( dst ) + 1 );\
		src = ( void * )( ( type * )( dst ) + 1 );\
	}\
	( len2 ) -= ( len1 ) * sizeof( type );\
}while( 0 )

static inline void block_xor( void * dst, void * src, int len )
{
	int tmp;

	while( len >= sizeof( long ) ){
		tmp = ( -( uintptr_t )dst ) % sizeof( long );
		_BLOCK_XOR_TYPE( dst, src, tmp, len, unsigned char );
		tmp = len / sizeof( long );
		_BLOCK_XOR_TYPE( dst, src, tmp, len, unsigned long );
	}
	_BLOCK_XOR_TYPE( dst, src, len, len, unsigned char );
}

static inline int block_compress( void * src, void * dst, int src_len, int dst_len )
{
	z_stream stream;
	int ret;

	stream.zalloc = Z_NULL;
	stream.zfree = Z_NULL;
	stream.opaque = Z_NULL;

	ret = deflateInit2( &stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_RLE );
	if( ret != Z_OK ){
		MOON_PRINT_MAN( ERROR, "zlib compress init error!" );
		return -1;;
	}
	stream.avail_in = src_len;
	stream.next_in = src;
	stream.avail_out = dst_len;
	stream.next_out = dst;
	ret = deflate( &stream, Z_FINISH );
	if( ret != Z_STREAM_END ){
		MOON_PRINT_MAN( ERROR, "zlib compress error!" );
		return -1;
	}
	deflateEnd( &stream );
	return stream.total_out;
}

static inline int block_uncompress( void * src, void * dst, int src_len, int dst_len )
{
	z_stream stream;
	int ret;

	stream.zalloc = Z_NULL;
	stream.zfree = Z_NULL;
	stream.opaque = Z_NULL;
	stream.avail_in = 0;
	stream.next_in = Z_NULL;
	ret = inflateInit2( &stream, -MAX_WBITS );
	if( ret != Z_OK ){
		MOON_PRINT_MAN( ERROR, "zlib uncompress init error!" );
		return -1;;
	}
	stream.avail_in = src_len;
	stream.next_in = src;
	stream.avail_out = dst_len;
	stream.next_out = dst;
	ret = inflate( &stream, Z_NO_FLUSH );
	if( ret != Z_STREAM_END ){
		MOON_PRINT_MAN( ERROR, "zlib uncompress error!" );
		return -1;
	}
	inflateEnd( &stream );
	return stream.total_out;
}

#define ID_OFFSET_LEN ( sizeof( packet_id_offset_s ) )

//第一次遍历过程，证明所有id类型都是合法的，并且所有的write都有原始版本
int  pac1_func( addr_pair p_pair, packet p_pac )
{
	mem_block p_block;
	int tmp1, tmp2, total_len;
	packet_change p_del, p_write, p_new;

	p_del   = &p_pac->changes[ 0 ];
	p_write = &p_pac->changes[ 1 ];
	p_new   = &p_pac->changes[ 2 ];
	p_block = p_pair->addr;
	tmp1 = get_struct_len( p_pac->to_all, p_block->type_id );
	tmp2 = get_struct_points_num( p_pac->to_all, p_block->type_id );
 	total_len = p_block->type_num * ( tmp1 + tmp2 * ID_OFFSET_LEN );
	if( tmp1 <= 0 || tmp2 < 0 ){
		MOON_PRINT_MAN( ERROR, "can't find the block type!" );
		return -1;
	}
	if( p_block->id == 0  ){//this is a new block
		p_new->num++;
		p_pac->new_block_size += total_len;
	}else if( p_block->is_del ){
		p_del->num++;
	}else{
		p_write->num++;
		p_pac->write_block_size += total_len;
	}
	return 0;
}

int  pac2_func( addr_pair p_pair, packet p_pac )
{
	mem_block p_block;
	addr_pair p_tmp_pair;
	version_info p_version;
	packet_change p_del, p_write, p_new;

	p_version = p_pac->version;
	p_del   = &p_pac->changes[ 0 ];
	p_write = &p_pac->changes[ 1 ];
	p_new   = &p_pac->changes[ 2 ];
	p_block = p_pair->addr;

	if( p_block->id == 0  ){//this is a new block
		p_block->tmp_id = p_pac->new_id_start + p_pac->cur_new_index;
		p_new->pairs[ p_pac->cur_new_index ].id = p_block->tmp_id;
		p_new->pairs[ p_pac->cur_new_index ].addr = p_block;
		p_pac->cur_new_index++;
	}else if( p_block->is_del ){
		p_del->pairs[ p_pac->cur_del_index ].id = p_block->id;
		p_del->pairs[ p_pac->cur_del_index ].addr = p_block;
		p_pac->cur_del_index++;
	}else{
		p_block->tmp_id = p_pac->write_id_start + p_pac->cur_write_index;
		p_write->pairs[ p_pac->cur_write_index ].id = p_block->id;
		p_write->pairs[ p_pac->cur_write_index ].addr = p_block;
		p_write->prev_pairs[ p_pac->cur_write_index ].id = p_block->id;
		p_tmp_pair = search_addr_id( p_version->order_id, p_block->id, p_version->block_num);
		if( p_tmp_pair == NULL || p_tmp_pair->id != p_block->id ){
			MOON_PRINT_MAN( ERROR, "can't find such block" );
			return -1;
		}
		p_write->prev_pairs[ p_pac->cur_write_index ].addr = p_tmp_pair->addr;
		p_pac->cur_write_index++;
	}
	return 0;
}

static inline void id_offset_write( packet_id_offset p_id_offset, uint64_t id, uint32_t offset )
{
	p_id_offset->id = id;
	p_id_offset->offset = offset;
	convert_uint64_t( &p_id_offset->id );
	convert_uint32_t( &p_id_offset->offset );
}

static inline int points_to_id_offset( char * id_offset, void ** points, int num, block_copy p_copy, version_info p_version, avl_tree p_avl )
{
	int j, tmp;
	unsigned offset;
	addr_pair p_tmp_pair;
	mem_block p_block;

	for( j = 0; j < num; j++ ){
		if( points[ j ] == NULL ){
			id_offset_write( &( ( packet_id_offset )id_offset )[ j ], 0, 0 );
			continue;
		}
		if( p_avl == NULL ){
			goto next1;
		}
		p_tmp_pair = avl_search( p_avl, points[ j ] );
		p_block = p_tmp_pair->addr;
		if( p_tmp_pair != NULL && get_virtual_address_info( p_block
				, points[ j ], &offset, NULL ) >= 0 ){
			if( p_tmp_pair->addr->is_del > 0 ){
				MOON_PRINT_MAN( WARNNING, "a point points to a del block!" );
				id_offset_write( &( ( packet_id_offset )id_offset )[ j ], 0, 0 );
			}
			tmp =get_target_offset( p_copy->src_type, p_copy->dst_type, offset
				, p_block->type_id, p_block->type_num );
			if( tmp < 0 ){
				MOON_PRINT_MAN( ERROR, "invaled point" );
				return -1;
			}
			id_offset_write( &( ( packet_id_offset )id_offset )[ j ], p_tmp_pair->addr->tmp_id, tmp );	
			continue;
		}
next1:
		p_tmp_pair = search_addr_id( p_version->order_addr
				, ( uintptr_t )points[ j ], p_version->block_num );
		p_block = p_tmp_pair->addr;
		if( p_tmp_pair != NULL && get_virtual_address_info( p_tmp_pair->addr					
				, points[ j ], &offset, NULL ) >= 0 ){
 			tmp =get_target_offset( p_copy->src_type, p_copy->dst_type, offset
				, p_block->type_id, p_block->type_num );
			if( tmp < 0 ){
				MOON_PRINT_MAN( ERROR, "invaled point" );
				return -1;
			}
	
			id_offset_write( &( ( packet_id_offset )id_offset )[ j ], p_tmp_pair->addr->id, tmp );
			continue;
		}
		MOON_PRINT_MAN( WARNNING, "a point is not the range of any block address!" );			
		id_offset_write( &( ( packet_id_offset )id_offset )[ j ], 0, 0 );
	}
}

static inline int id_offset_to_points( void ** points, packet_id_offset p_id_offset, int num, block_copy p_copy, version_info p_version )
{
	int i, tmp;
	addr_pair p_pair;
	mem_block p_block;

	for( i = 0; i< num; i++ ){
		convert_uint64_t( &p_id_offset[ i ].id );
		convert_uint32_t( &p_id_offset[ i ].offset );
		p_pair = search_addr_id( p_version->order_addr
				, p_id_offset[ i ].id, p_version->block_num );
		if( p_pair == NULL ){
			MOON_PRINT_MAN( ERROR, "can't find this poins-id" );
			return -1;
		}
		p_block = p_pair->addr;
		tmp =get_target_offset( p_copy->src_type, p_copy->dst_type
				, p_id_offset[ i ].offset, p_block->type_id, p_block->type_num );
		if( tmp < 0 ){
			MOON_PRINT_MAN( ERROR, "illegal points-offset" );
			return -1;
		}
		points[ i ] = ( char * )p_block->virtual_addr + tmp;
	}
	return 0;
}

#define _CONVERT_ELEM( num, pp_dst, cur_type, prev_bit_len, p_len ) ({\
	int _cur_type_len, _big;\
	cur_type * _p;\
\
	_big = 0;\
	_p = ( cur_type * )*( pp_dst );\
	_cur_type_len = ( sizeof( cur_type ) << 3 ) - 1;\
	*_p = ( ( ( num ) >> ( prev_bit_len ) ) & ( ( uint64_t )-1 >> ( 64 - _cur_type_len ) ) );\
	if( ( num ) >= ( uint64_t )1 << ( _cur_type_len + ( prev_bit_len ) ) ){\
		*_p += ( ( uint64_t )1 << _cur_type_len );\
		_big = 1;\
		( prev_bit_len ) += _cur_type_len;\
	}\
	convert_##cur_type( _p );\
	_p++;\
	*( pp_dst ) = _p;\
	*( p_len ) += sizeof( cur_type );\
	_big;\
})

//|2B|2B|4B
static inline int convert_to_moon_num( void ** ppc, uint64_t num, int * p_len )
{
	int bits;

	bits = 0;
	if( num >= ( uint64_t )1 << 62 ){
		return -1;
	}
	if( _CONVERT_ELEM( num, ppc, uint16_t, bits, p_len ) == 0 ){
		return 0;
	}
	if( _CONVERT_ELEM( num, ppc, uint16_t, bits, p_len ) == 0 ){
		return 0;
	}
	_CONVERT_ELEM( num, ppc, uint32_t, bits, p_len );
	return 0;
}

#define _GET_ELEM( p_num, ppc, p_len, cur_type, prev_bit_len ) ({\
	cur_type * _p;\
	int _cur_type_len = ( sizeof( cur_type ) << 3 ) - 1;\
	int _ret;\
\
	_ret = -1;\
	if( *( p_len ) >= sizeof( cur_type ) ){\
		_p = ( cur_type * )*( ppc );\
		convert_##cur_type( _p );\
		*( p_num ) += ( *_p & ~( ( uint64_t )1 << _cur_type_len ) ) << ( prev_bit_len );\
		if( ( *_p & ( ( uint64_t )1 << _cur_type_len ) ) > 0 ){\
			( prev_bit_len ) += _cur_type_len;\
			_ret = 1;\
		}else{\
			_ret = 0;\
		}\
		_p++;\
		*( ppc ) = _p;\
		*( p_len ) -= sizeof( cur_type );\
	}\
	_ret;\
})

static inline int get_from_moon_num( void **ppc, uint64_t * p_num, int * p_len )
{
	int ret, bits;

	*p_num = 0;
	bits = 0;
	ret = _GET_ELEM( p_num, ppc, p_len, uint16_t, bits );
	if( ret <= 0 ){
		return ret;
	}
	ret = _GET_ELEM( p_num, ppc, p_len, uint16_t, bits );
	if( ret <= 0 ){
		return ret;
	}
	ret = _GET_ELEM( p_num, ppc, p_len, uint32_t, bits );
	if( ret < 0 ){
		return ret;
	}else if( ret > 0 ){
		*p_num += ( uint64_t )1 << bits;
	}
	return 0;
}
//这里是测试convert_to_moon_num,get_from_moon_num这两个函数的。
#ifdef LEVEL_TEST

static int moon_num_len( uint64_t num )
{
	int len;

	if( num < 1 << 15 ){
		len = 2;
	}else if( num < 1 << 30 ){
		len = 4;
	}else{
		len = 8;
	}		
	return len;
}

int test_moon_num( )
{
	char buf[ 8 ], * pc;
	int len;
	uint64_t num;
	uint64_t i, j;
	uint64_t test_set[] = { 0, 1000, ( 1 << 15 ) - 1000, ( 1 << 15 ) + 1000
			, ( 1 << 30 ) - 1000, ( 1 << 30 ) + 1000
			, ( ( uint64_t )1 << 62 ) - 1000, ( ( uint64_t )1 << 62 ) + 1000 };

	for( j = 0; j < sizeof( test_set ) / sizeof( test_set[ 0 ] ); j += 2 ){
		for( i = test_set[ j ]; i < test_set[ j + 1 ]; i++ ){
			pc = buf;
			if( i >= ( uint64_t )1 << 62 ){
				if( convert_to_moon_num( ( void ** )&pc, i, &len ) >= 0 ){
					MOON_PRINT_MAN( TEST, "can't process large num!" );
					return -1;
				}else{
					continue;
				}
			}
			if( convert_to_moon_num( ( void ** )&pc, i, &len ) < 0 
					|| moon_num_len( i )!= len ){
				MOON_PRINT_MAN( TEST, "convert error!" );
				return -1;
			}
			pc = buf;
			if( get_from_moon_num( ( void ** )&pc, &num, &len ) < 0 
					|| len != 0 || i != num ){
				MOON_PRINT_MAN( TEST, "%lld:%lld:%d get error!", i, num, len );
				return -1;
			}
		}
	}
	return 0;
}

#endif

#define IF_FREE( p ) {\
	if( p != NULL ){\
		free( p );\
	}\
}
static inline int convert_blocks_to_buf( char * buf, addr_pair p_pair, int num, block_copy p_copy, version_info p_version, avl_tree p_avl, int md5 )
{
	char  * id_offset;
	int i, tmp1, tmp2;
	mem_block p_block;
	void ** points;

	id_offset = buf;
	for( i = 0; i < num; i++ ){
		p_block = p_pair[ i ].addr;

		p_copy->src = p_block->data;
		p_copy->index = p_block->type_id;
		p_copy->num = p_block->type_num;
		p_copy->dst = id_offset;

		tmp1 = get_struct_len( p_copy->dst_type, p_block->type_id );
		tmp2 = get_struct_points_num( p_copy->dst_type, p_block->type_id );
		id_offset += p_block->type_num * tmp1;
		tmp1 = p_block->type_num * tmp2;
		points = ( void ** )( id_offset + tmp1 * ID_OFFSET_LEN );

		p_copy->points = points - tmp1;
		if( get_block_copy( p_copy ) < 0 
					|| points_to_id_offset( id_offset, points - tmp1
						, tmp1, p_copy, p_version, p_avl  ) < 0 ){
			MOON_PRINT_MAN( ERROR, "point to id offset error!" );
			return -1;
		}
		id_offset = ( char * )points;
		if( md5 ){
			MD5( p_copy->dst, id_offset - ( char * )p_copy->dst, p_block->md5_sum );
		}
	}
	return 0;
}
static inline int convert_blocks_from_buf( addr_pair p_pair, char * buf, int num, block_copy p_copy, version_info p_version )
{
	int i, tmp1, tmp2, len;
	char *pc;
	mem_block p_block;

	pc = buf;
	for( i = 0; i < num; i++ ){
		p_block = p_pair[ i ].addr;

		p_copy->src = pc;
		p_copy->dst = p_block->data;
		p_copy->index = p_block->type_id;
		p_copy->num = p_block->type_num;
		
		tmp1 = get_struct_len( p_copy->src_type, p_block->type_id );
		tmp2 = get_struct_points_num( p_copy->dst_type, p_block->type_id );
		len = p_block->type_num * ( tmp1 + tmp2 * ID_OFFSET_LEN );
		p_copy->points = ( void ** )( pc + p_block->type_num * tmp1 );
		
		MD5( pc, len, p_block->md5_sum );
		if( id_offset_to_points( p_copy->points, ( packet_id_offset )p_copy->points, p_block->type_num * tmp2, p_copy, p_version ) < 0 ){
			MOON_PRINT_MAN( ERROR, "id offset to points error!" );
			return -1;;
		}
		if( get_block_copy( p_copy ) < 0 ){
			MOON_PRINT_MAN( ERROR, "dump block error!" );
			return -1;;
		}
		pc += len;
	}
	return 0;
}

static inline int  packet_del_data( packet_change p_change )
{
	uint64_t prev_64, next_64;
	char * pc;
	int i;

	prev_64 = p_change->num;
	pc = ( char * )p_change->pairs;
	next_64 = 0;
	for( i = 0; i < p_change->num + 1; i++ ){
		next_64 = p_change->pairs[ i ].id - next_64;//差分
		if( convert_to_moon_num( ( void **)&pc, prev_64, &p_change->len ) < 0  ){
			MOON_PRINT_MAN( ERROR, "convvert to moon num error!" );
			return -1;
		}
		prev_64 = next_64;
	}
}	

static inline int packet_new_data( packet_change p_change )
{	
	char * pc;
	int i;
	mem_block p_block;
	
	pc = ( char * )p_change->pairs;
	if( convert_to_moon_num( ( void ** )&pc, p_change->num, &p_change->len ) < 0 ){
		MOON_PRINT_MAN( ERROR, "convert to moon num error!" );
		return -1;
	}
	for( i = 0; i < p_change->num; i++ ){
		p_block = p_change->pairs[ i ].addr;
		if( convert_to_moon_num( ( void ** )&pc, p_block->type_id, &p_change->len ) < 0 
				||convert_to_moon_num( ( void ** )&pc, p_block->type_num, &p_change->len ) < 0 ){
			MOON_PRINT_MAN( ERROR, "convert to moon num error!" );
			return -1;
		}
	}
}

int get_commit_packet( packet_model p_model )
{
	thread_private tp;
	packet_s pac;
	packet_change p_del, p_write, p_new;
	packet_data p_data;
	packet_data_head p_data_head;
	packet_update_head p_update_head;
	packet_head p_head;
	int i, j, k, tmp1, tmp2, tmp3, ret;
	mem_block p_block;
	version_info p_version;
	char *pc, *prev_data;
	block_copy_s copy_block;	
	MD5_CTX md5_ctx;

	tp = shadow_env_get();
	prev_data = NULL;
	ret = -1;
	if( tp == NULL || tp->avl == NULL || tp->version == NULL ){
		MOON_PRINT_MAN( WARNNING, "dont't change anyting in this version" );
		return -1;
	}
	p_version = tp->version;
	memset( &pac, 0, sizeof( pac ) );

	pac.from_all = p_version->entity->shadow_type->struct_types;
	pac.to_all = p_version->entity->remote_all;
	pac.version = p_version;

	copy_block.flag = 0;
	copy_block.src_type = pac.from_all;
	copy_block.dst_type = pac.to_all;

	pthread_mutex_lock( &tp->avl_mutex );
	//第一次遍历avl
	if( avl_traver_first( tp->avl, pac1_func, &pac ) < 0 ){
		MOON_PRINT_MAN( ERROR, "traver avl first time error!" );
		goto back1;
	}
	//处理del
	p_del = &pac.changes[ 0 ];
	if( p_del->num > 0 ){
		p_del->pairs = calloc( sizeof( addr_pair_s ), p_del->num + 1 );
	}
	//处理write
	p_write = &pac.changes[ 1 ];
	if( p_write->num <= 0 ){
		MOON_PRINT_MAN( ERROR, "no blocks write");
		goto back1;
	}
	pac.write_id_start = p_version->order_id[ p_version->block_num - 1 ].id + 1;
	p_write->pairs = calloc( sizeof( addr_pair_s ), p_write->num + 1 );
	p_write->prev_pairs = calloc( sizeof( addr_pair_s ), p_write->num );
	//处理new
	p_new = &pac.changes[ 2 ];
	pac.new_id_start = pac.write_id_start + p_write->num;
	if( p_new->num > 0 ){
		p_new->pairs = calloc( sizeof( addr_pair_s ), p_new->num + 1 );
	}
	//处理data
	p_data = &pac.data;
	p_data_head = &p_data->head;
	p_data_head->original_len = pac.write_block_size + pac.new_block_size;
	p_data->data = ( unsigned char * )calloc( 1, p_data_head->original_len );
	p_data->len = p_data_head->original_len;
	prev_data = ( unsigned char * )calloc( 1, pac.write_block_size );
	if( ( p_del->num > 0 && p_del->pairs == NULL ) || p_data->data == NULL
			|| ( p_write->pairs == NULL || p_write->prev_pairs == NULL ) 
			|| ( p_new->num > 0 && p_new->pairs == NULL ) || prev_data == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		goto back1;
	}
	//第二次遍历avl
	if( avl_traver_first( tp->avl, pac2_func, &pac ) < 0 ){
		MOON_PRINT_MAN( ERROR, "second travel avl error!" );
		goto back1;
	}
	qsort_addr_pair( p_del->pairs, p_del->num );
	qsort_addr_pair( p_write->pairs, p_write->num );
	qsort_addr_pair( p_write->prev_pairs, p_write->num );
	//先处理write块的指针转换
	if( convert_blocks_to_buf( p_data->data, p_write->pairs
					, p_write->num, &copy_block, p_version, tp->avl, 1 ) < 0 
			|| convert_blocks_to_buf( p_data->data + pac.write_block_size, p_new->pairs
					, p_new->num, &copy_block, p_version, tp->avl, 1 ) < 0 
			|| convert_blocks_to_buf( prev_data, p_write->prev_pairs
					, p_write->num, &copy_block, p_version, NULL, 0 ) < 0 ){
		MOON_PRINT_MAN( ERROR, "convert blocks to buffer error!" );
		goto back1;
	}
	pthread_mutex_unlock( &tp->avl_mutex );
	//计算所有block加起来的md5
	p_update_head = &pac.update_head;	
	MD5_Init( &md5_ctx );
	for( i = 0, j = 0, k = 0; i < p_version->block_num; i++ ){
		p_block = p_version->order_id[ i ].addr;
		if( j < p_del->num && p_block->id == p_del->pairs[ j ].id ){
			j++;
			continue;
		}else if( k < p_write->num && p_block->id == p_write->pairs[ k ].id ){
			k++;
			continue;
		}
		MD5_Update( &md5_ctx, p_block->md5_sum, sizeof( p_block->md5_sum ) );
	}
	for( i = 0; i < p_write->num; i++ ){
		p_block = p_write->pairs[ i ].addr;
		MD5_Update( &md5_ctx, p_block->md5_sum, sizeof( p_block->md5_sum ) );
	}
	for( i = 0; i < p_new->num; i++ ){
		p_block = p_new->pairs[ i ].addr;
		MD5_Update( &md5_ctx, p_block->md5_sum, sizeof( p_block->md5_sum ) );
	}
	MD5_Final( p_update_head->to_md5, &md5_ctx );
	memcpy( p_update_head->from_md5, p_version->md5_sum, sizeof( p_update_head->from_md5 ) );
	//计算修改的block对应的原始版本，并与现在的异或
	block_xor( p_data->data, prev_data, pac.write_block_size );
	p_data_head->mask |= IS_XOR;
	//压缩整个数据快
	IF_FREE( prev_data );
	prev_data = ( char * )calloc( 1, p_data->len );
	if( prev_data == NULL 
			|| block_compress( p_data->data, prev_data, p_data->len, p_data->len ) < 0 ){
		MOON_PRINT_MAN( ERROR, "compress error" );
		goto back2;
	}
	free( p_data->data );
	p_data->data = prev_data;
	p_data->len = tmp1;
	p_data_head->mask |= IS_COMPRESS;
	prev_data = NULL;
	//打包del数据
	if( p_del->num > 0 ){
		p_update_head->info_bits |= HAS_DELS;
		if( packet_del_data( p_del ) < 0 ){
			MOON_PRINT_MAN( ERROR, "packet dels data error!" );
			goto back2;
		}
	}
	//打包write数据
	p_update_head->info_bits |= HAS_WRITES;
	if( packet_del_data( p_write ) < 0 ){
		MOON_PRINT_MAN( ERROR, "packet writes data error!" );
		goto back2;
	}
	//打包new数据
	if( p_new->num > 0 ){
		p_update_head->info_bits |= HAS_NEWS;
		if( packet_new_data( p_new ) < 0 ){
			MOON_PRINT_MAN( ERROR, "packet new data error!" );
			goto back2;
		}
	}
	//把所有的数据拷贝到一个内存快中
	p_head = &pac.head;
	memcpy( p_head->version, "M0", sizeof( p_head->version ) );
	p_head->cmd = MOON_UPDATE;
	p_head->len = sizeof( pac.head ) + sizeof( pac.update_head ) 
			+ p_del->len + p_write->len + p_new->len + sizeof( *p_data_head ) + p_data->len;
	memcpy( p_head->md5_sum, p_version->entity->md5_sum, sizeof( p_head->md5_sum ) );

	if( set_packet_elem_len_position( p_model, p_head->len, 0 ) < 0 
			|| create_packet_buf( p_model ) < 0 ){
		MOON_PRINT_MAN( ERROR, "create packet error!" );
		goto back2;
	}
	pc = get_packet_elem_buf( p_model );
	convert_uint32_t( &p_head->len );
	convert_uint16_t( &p_head->cmd );
	memcpy( pc, p_head, sizeof( *p_head ) );
	pc += sizeof( *p_head );

	convert_uint16_t( &p_update_head->info_bits );
	memcpy( pc, p_update_head, sizeof( *p_update_head ) );
	pc += sizeof( *p_update_head );
	
	for( i = 0; i < 3; i++ ){
		if( pac.changes[ i ].len > 0 ){
			memcpy( pc, pac.changes[ i ].pairs, pac.changes[ i ].len );
			pc += pac.changes[ i ].len;
		}
	}
	if( p_data->len > 0 ){
		convert_uint16_t( &p_data_head->mask );
		convert_uint32_t( &p_data_head->original_len );
		memcpy( pc, p_data_head, sizeof( *p_data_head ) );
		pc += sizeof( *p_data_head );
		memcpy( pc, p_data->data, p_data->len );
	}
	ret = 0;
	goto back2;
back1:
	pthread_mutex_unlock( &tp->avl_mutex );
back2://释放分配的内存
	IF_FREE( prev_data );
	IF_FREE( p_del->ids );
	IF_FREE( p_write->pairs );
	IF_FREE( p_write->prev_pairs );
	IF_FREE( p_new->pairs );
	IF_FREE( p_data->data );
	return ret;
}

static inline int dump_mem_block( mem_block p_block , version_info p_version, unsigned long long id, int index )
{
	mem_block p_dst_block;
	addr_pair p_pair;

	p_dst_block = ( mem_block )calloc( 1, sizeof( mem_block_s ) + p_block->data_len );
	if( p_dst_block == NULL ){
		return -1;
	}
	p_dst_block->data = ( char * )( p_dst_block + 1 );
	if( p_dst_block->is_root > 0 ){
		p_dst_block->is_root = 2;
		p_dst_block->virtual_addr = p_block->virtual_addr;
	}else{
		p_dst_block->virtual_addr = p_dst_block->data;
	}
	p_dst_block->type_id = p_block->type_id;
	p_dst_block->type_num = p_block->type_num;
	p_dst_block->data_len = p_block->data_len;
	p_dst_block->id = id;
	p_dst_block->version = p_version->version;
	p_pair = &p_version->order_id[ index ];
	p_pair->id = id;
	p_pair->addr = p_dst_block;
	return 0;
}
static inline int create_new_block( int type_id, int type_num, version_info p_version, unsigned long long id, int index )
{
	int len;
	mem_block p_block;
	addr_pair p_pair;

	len = get_struct_len( p_version->entity->shadow_type->struct_types, type_id );
	len *= type_num;
	if( len <= 0 ){
		MOON_PRINT_MAN( ERROR, "a type of struct error!" );
		return -1;
	}
	p_block = ( mem_block )calloc( sizeof( mem_block_s ) + len, 1 );
	if( p_block == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		return -1;
	}
	p_block->version = p_version->version;
	p_block->id = id;
	p_block->virtual_addr = p_block + 1;
	p_block->type_id = type_id;
	p_block->type_num = type_num;
	p_block->data_len = len;
	p_block->data = p_block->virtual_addr;
	p_pair = &p_version->order_id[ index ];
	p_pair->id = p_block->id;
	p_pair->addr = p_block;
	return 0;
}

static inline void version_surface_free( version_info p_version )
{	
	if( p_version != NULL ){
		IF_FREE( p_version->order_id );
		IF_FREE( p_version->order_id );
		IF_FREE( p_version->del_blocks );
		free( p_version );
	}
}
static inline version_info dump_version( version_info p_version, int block_num, int del_num )
{
	version_info p_new_version = NULL;

	p_new_version = ( version_info )calloc( sizeof( version_info_s ), 1 );
	if( p_new_version == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		goto error;
	}
	p_new_version->block_num = block_num;
	p_new_version->order_id = ( addr_pair )calloc( sizeof( addr_pair_s ), block_num );
	if( p_new_version->order_id == NULL ){
		free( p_new_version );
		MOON_PRINT_MAN( ERROR, "malloc error!");
		goto error;
	}
	p_new_version->del_num = del_num;
	p_new_version->del_blocks = ( mem_block * )calloc( sizeof( mem_block ), del_num );
	if( p_new_version->del_blocks == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error" );
		goto error;
	}
	p_new_version->last_version = p_version->version;
	p_new_version->version = p_new_version->last_version + 1;
	p_new_version->ref_num = 1;
	p_new_version->entity = p_version->entity;
	return p_new_version;
error:
	version_surface_free( p_new_version );
	return NULL;
}

static inline int unpack_del_data( packet_change p_change, char ** pp_buf, int * p_len )
{
	uint64_t u64_tmp, u64_tmp2;
	int i;

	if( get_from_moon_num( ( void ** )pp_buf, &u64_tmp, p_len ) < 0 ){
		MOON_PRINT_MAN( ERROR, "get dels num error!" );
		return -1;
	}
	p_change->num = u64_tmp;
	p_change->len = sizeof( *p_change->ids ) * p_change->num;
	p_change->ids = malloc( p_change->len );
	if( p_change->ids == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		return -1;
	}
	u64_tmp = 0;
	for( i = 0; i < p_change->num; i++ ){
		if( get_from_moon_num( ( void ** )pp_buf, &u64_tmp2, p_len ) < 0 ){
			MOON_PRINT_MAN( ERROR, "get dels ids error!" );
			return -1;
		}
		u64_tmp += u64_tmp2;
		p_change->ids[ i ] = u64_tmp;
	}
	return 0;
}

static inline int unpack_new_data( packet_change p_change, char ** pp_buf, int * p_len )
{
	uint64_t u64_tmp, u64_tmp2;
	int i;
	addr_pair p_pair;

	if( get_from_moon_num( ( void ** )pp_buf, &u64_tmp, p_len ) < 0 ){
		MOON_PRINT_MAN( ERROR, "get news num error!" );
		return -1;
	}
	p_change->num = u64_tmp;
	p_change->len = sizeof( addr_pair_s ) * p_change->num;
	p_change->pairs = ( addr_pair )malloc( p_change->len );
	if( p_change->pairs == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		return -1;
	}
	for( i = 0; i < p_change->num; i++ ){
		p_pair = &p_change->pairs[ i ];
		if( get_from_moon_num( ( void ** )pp_buf, &u64_tmp, p_len ) < 0 
				|| get_from_moon_num( ( void ** )pp_buf, &u64_tmp2, p_len ) < 0 ){
			MOON_PRINT_MAN( ERROR, "get new block info error!" );
			return -1;
		}
		p_pair->id = u64_tmp;
		p_pair->num_data = u64_tmp2;
	}
	return 0;
}

int _create_new_version( version_info p_version, packet_model p_model )
{
	packet_s pac;
	packet_head p_head;
	packet_update_head p_update_head;
	packet_change p_del, p_write, p_new;
	packet_data p_data;
	packet_data_head p_data_head;
	int  ret, len, i, j, k, n, m, write_index, new_index;
	MD5_CTX ctx;
	char md5_buf[ 16 ];
	char * pac_buf, * pac_end, * prev_data;
	addr_pair p_pair;
	version_info p_new_version;
	mem_block p_block;
	block_copy_s copy;
	
	if( p_version == NULL || p_model == NULL 
				|| next_packet_elem( p_model ) < 0 ){
		MOON_PRINT_MAN( ERROR, "input paraments error!" );
		return -1;
	}
	memset( &pac, 0, sizeof( pac ) );
	p_new_version = NULL;
	prev_data = NULL;
	ret = -1;

	pac.version = p_version;
	pac.to_all = p_version->entity->shadow_type->struct_types;
	pac.from_all = p_version->entity->remote_all;

	pac_buf = get_packet_elem_buf( p_model );
	len = get_packet_elem_len( p_model );
	p_head = ( packet_head )get_packet_elem_data_position( p_model, -1 );
	if( p_head == NULL 
			|| len < p_head->len || pac_buf == NULL ){
		MOON_PRINT_MAN( ERROR, "packet analyse error!" );
		return -1;
	}
	pac.head = *p_head;
	p_head = &pac.head;
	if( len > p_head->len ){
		len = p_head->len;
	}
	pac_end = pac_buf + len;
	//解析update_head
	p_update_head = &pac.update_head;
	if( pac_end - pac_buf < sizeof( *p_update_head ) ){
		MOON_PRINT_MAN( ERROR, "too short to get update head!" );
		return -1;
	}
	*p_update_head = *( ( packet_update_head )pac_buf );
	pac_buf = ( char * )( ( packet_update_head )pac_buf + 1 );
	convert_uint16_t( &p_update_head->info_bits );
	if( memcmp( p_update_head->from_md5, p_version->md5_sum
			, sizeof( p_update_head->from_md5 ) ) != 0 ){
		MOON_PRINT_MAN( ERROR, "dont't base on this version!" );
		return -1;
	}
	if( ( p_update_head->info_bits & HAS_WRITES ) == 0 ){
		MOON_PRINT_MAN( ERROR, "no update blocks!"  );
		return -1;
	}
	len = pac_end - pac_buf;
	//处理del
	p_del = &pac.changes[ 0 ];
	if( ( p_update_head->info_bits & HAS_DELS ) 
			&& unpack_del_data( p_del, &pac_buf, &len ) < 0 ){
		MOON_PRINT_MAN( ERROR, "unpack dels data error!" );
		goto error;
	}
	//处理write
	p_write = &pac.changes[ 1 ];
	pac.write_id_start = p_version->order_id[ p_version->block_num - 1 ].id + 1;
	if( unpack_del_data( p_write, &pac_buf, &len ) < 0 ){
		MOON_PRINT_MAN( ERROR, "unpack writes data error!" );
		goto error;
	}
	//处理new
	p_new = &pac.changes[ 2 ];
	pac.new_id_start = pac.write_id_start + p_write->num;
	if( ( p_update_head->info_bits & HAS_NEWS ) 
			&& unpack_new_data( p_new, &pac_buf, &len ) < 0 ){
		MOON_PRINT_MAN( ERROR, "unpack new data error!" );
		goto error;
	}
	//创建新的version
	n = p_version->block_num + p_new->num - p_del->num;
	m = p_write->num + p_del->num;
	write_index = p_version->block_num - m;
	new_index = write_index + p_write->num;
	if( n < 0 || p_write->num <= 0 || m > p_version->block_num ){
		MOON_PRINT_MAN( ERROR, "total or dels blocks num under zero!" );
		goto error;
	}
	p_new_version = dump_version( p_version, n, m );
	if( p_new_version == NULL ){
		MOON_PRINT_MAN( ERROR, "dump new version error!");
		goto error;
	}
	for( i = 0, j = 0, k = 0, n = 0, m = 0; i < p_version->block_num; i++ ){
		p_pair = &p_version->order_id[ i ];
		if( j < p_del->num ){
			if( p_del->ids[ j ] < p_pair->id ){
				MOON_PRINT_MAN( ERROR, "illegal del block id" );
				goto error;
			}else if( p_del->ids[ j ] == p_pair->id ){
				j++;
				p_new_version->del_blocks[ m++ ] = p_pair->addr;
				continue;
			}
		}
		if( k < p_write->num ){
			if( p_write->ids[ k ] < p_pair->id ){
				MOON_PRINT_MAN( ERROR, "illegal write block id" );
				goto error;
			}else if( p_write->ids[ k ] == p_pair->id ){
				p_new_version->del_blocks[ m++ ] = p_pair->addr;
				if( dump_mem_block( p_pair->addr, p_new_version
						, pac.write_id_start + k, write_index + k ) < 0 ){
					MOON_PRINT_MAN( ERROR, "dump write block error!" );
					goto error;
				}
				pac.write_block_size += get_struct_len( pac.from_all, p_block->type_id ) 
						* p_block->type_num;
				k++;
				continue;
			}
		}
		p_new_version->order_id[ n++ ] = *p_pair;
	}
	if( j < p_del->num || k < p_write->num ){
		MOON_PRINT_MAN( ERROR, "have illegal blocks" );
		goto error;
	}
	for( i = 0; i < p_new->num; i++ ){
		p_pair = &p_new->pairs[ i ];
		if( create_new_block( p_pair->id, p_pair->num_data, p_new_version
				, pac.new_id_start + i, new_index + i ) < 0 ){
			MOON_PRINT_MAN( ERROR, "create new block error!" );
			goto error;
		}
		pac.new_block_size += get_struct_len( pac.from_all, p_block->type_id ) * p_block->type_num;
	}
	p_new_version->order_addr = sort_addr_id( p_new_version->order_id, p_new_version->block_num );
	if( p_new_version->order_addr == NULL ){
		MOON_PRINT_MAN( ERROR, "create order addr array error!" );
		goto error;
	}
	//处理data
	p_data = &pac.data;
	p_data_head = &p_data->head;
	if( pac_end - pac_buf < sizeof( *p_data_head ) ){
		MOON_PRINT_MAN( ERROR, "get data head error" );
		goto error;
	}
	*p_data_head = * ( ( packet_data_head )pac_buf );
	pac_buf = ( char * )( ( packet_update_head )pac_buf + 1 );
	convert_uint32_t( &p_data_head->original_len );
	convert_uint16_t( &p_data_head->mask );
	p_data->len = p_data_head->original_len;
	if( p_data->len != pac.write_block_size + pac.new_block_size ){
		MOON_PRINT_MAN( ERROR, "data size too short!" );
		goto error;
	}
	p_data->data = ( char * )calloc( 1, p_data->len );
	prev_data = ( char * )calloc( 1, pac.write_block_size );
	if( p_data == NULL || prev_data == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		goto error;
	}
	len = block_uncompress( pac_buf, p_data->data, pac_end - pac_buf, p_data->len );
	if( len != p_data->len ){
		MOON_PRINT_MAN( ERROR, "data uncompress error!" );
		goto error;
	}
	copy.flag = 0;
	copy.src_type = pac.to_all;
	copy.dst_type = pac.from_all;
	if( convert_blocks_to_buf( prev_data, p_new_version->order_id + write_index
			, p_write->num, &copy, p_new_version, NULL, 0 ) < 0 ){
		MOON_PRINT_MAN( ERROR, "convert block to buf error!" );
		goto error;
	}
	block_xor( p_data->data, prev_data, pac.write_block_size );
	copy.flag = 1;
	copy.src_type = pac.from_all;
	copy.dst_type = pac.to_all;
	if( convert_blocks_from_buf( p_new_version->order_id + write_index, pac_buf
			, p_write->num + p_new->num, &copy, p_new_version ) < 0 ){
		MOON_PRINT_MAN( ERROR, "get blocks from buf error!" );
		goto error;
	}
	//做md5校验
	MD5_Init( &ctx );
	for( i = 0; i < p_new_version->block_num; i++ ){
		p_block = p_new_version->order_id[ i ].addr;
		MD5_Update( &ctx, p_block->md5_sum, sizeof( p_block->md5_sum ) );
	}
	MD5_Final( p_new_version->md5_sum, &ctx );
	if( memcmp( p_new_version->md5_sum, p_update_head->to_md5
			, sizeof( p_new_version->md5_sum ) ) != 0 ){
		MOON_PRINT_MAN( ERROR, "md5 not equal!" );
		goto error;
	}
	//保存new version
	set_packet_elem_data_position( p_model, p_new_version, 0 );
	ret = 0;
	goto back;
error:
	if( p_new_version != NULL && p_new_version->order_id != NULL ){
		for( i = write_index; i < p_new_version->block_num; i++ ){
			IF_FREE( p_new_version->order_id[ i ].addr );
		}
	}
	version_surface_free( p_new_version );
back:
	IF_FREE( prev_data );
	IF_FREE( p_del->ids );
	IF_FREE( p_write->pairs );
	IF_FREE( p_new->pairs );
	IF_FREE( p_data->data );
	return ret;
}

