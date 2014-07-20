#include<stdio.h>
#include<stdlib.h>
#include<pthread.h>
#include<string.h>
#include"zlib.h"
#include"moon_runtime.h"
#include"moon_debug.h"
#include"shadow_struct.h"
#include<openssl/md5.h>
#include"moon_packet2.h"
#include"moon_common.h"
#include<sys/time.h>

//版本号从0开始
struct version_info_s{ // [ double_list_s + hash_key_s ] + double_list_s + version_info_s 
	int ref_num;
	int status;
	int user_num;//使用这个版本的用户数
	unsigned long version;
	int block_num;
	addr_pair order_addr; //按虚拟地址排序的数组
	id_pair order_id; //按id排序的数组
	shadow_entity entity;
	unsigned char md5_sum[ 16 ];//所有块按id排序后所有块md5的md5.
	buf_head p_buf_head;
};
typedef struct version_info_s version_info_s;
typedef struct version_info_s * version_info;

enum{
	VERSION_STATUS_NORMAL = 0x0,
	VERSION_STATUS_OLD = 0x1,
	VERSION_STATUS_CLOSED = STATUS_CLOSED
};

struct mem_block_s{ //mem_block_s + data
	int ref_num;
	unsigned long long id; //内存id，递增的，从1开始。
	unsigned long long tmp_id;//只在生成一个提交时临时保存新分配的id
	unsigned long version;//最初创建时属于那个版本。
	void * virtual_addr;//最初分配的块的地址或静态变量的地址
	struct mem_block_s * p_original_block;//最初分配的内存或静态变量所属块
	int is_del;
	int is_root; //0 不是，1是全局变量，2是副本
	int type_id;//对应的类型的序号
	int type_num;//元素个数
	int data_len;//数据快总长度
	unsigned char md5_sum[ 16 ];//按主体的结构指针在前数据在后计算的md5
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

enum{
	POINT_INIT = 0x0,
	POINT_LINKED = 0x1,
	POINT_CLOSED = STATUS_CLOSED
};

typedef struct{
	int ( * can_send_func )( linked_point p_point );
	int ( * can_recv_func )( linked_point p_point, buf_head p_buf_head );
	int ( * close_func )( linked_point p_point );
	common_user_data_u inter_user_data[ 4 ];
	common_user_data_u sned_user_data[ 4 ];
	common_user_data_u recv_user_data[ 4 ];
} point_user_interface_s;
typedef point_user_interface_s * point_user_interface;

typedef struct{
	int ref_num;
	int status;
	shadow_entity p_entity;
	stream_interface_s stream_i;
	point_user_interface_s user_i;
}linked_point_s;
typedef linked_point_s * linked_point;

//以前的结构太简单，现在要改为可有多个镜像类别，每个类别又可有多个实体。

//当status < 0 且 total_user_num == 0 时关闭entity
#define IS_ENTITY_CLOSED( p_entity ) ( p_entity->status < 0 && p_entity->total_user_num == 0 )
struct shadow_entity_s{//[ dlist_s + hash_key_s ] + shadow_entity_s
//	char xid[256]; //标识一个实体的
	int ref_num;
	int status;//<0 关闭，可逆
	unsigned char md5_sum[ 16 ];//md5;type + xid
	buf_head init_ack_pac;//MOON_INIT_ACK packet
	shadow_type shadow_type;
	double_list_s version_table[ HASH_NUM ];
	version_info root_version;//最开始的版本
	version_info version_head;
	version_info version_tail;
	version_info cur_notice_version;//只有本体端用
	int version_alive_num;
	int version_num;
	int total_user_num;
	pthread_mutex_t version_mutex;
	struct_all remote_all;
	int is_shadow;//0为本体端，1为影子端
	double_list_s related_points;//关联的本体或影子端 double_list_s + linked_point_s
	int points_num;
	linked_point next_notice_point;//只有本体端用
	//用于影子端
	unsigned long long query_timer;
	struct timeval last_quuery_tv;
	unsigned long long packet_id;//从1开始,递增
	update_packet up_head;
	update_packet up_tail;
	int last_bigest_version;//用于update包判断是否回退
	int packets_num;//限制数量
	enum{
		int query_status;//保证唯一性
		int notice_status;
	}
};
typedef struct shadow_entity_s shadow_entity_s;
typedef struct shadow_entity_s * shadow_entity;

typedef{
	unsigned long long id;
	int version;
	buf_head p_buf_head;
} update_packet_s;
typedef update_packet_s * update_packet;

enum{
	SERVER_MAX_VERSION_NUM = 128,
	CLIENT_MAX_VERSION_NUM = SERVER_MAX_VERSION_NUM / 2,
	RESET_MIN_LATER_VERSION_NUM = CLIENT_MAX_VERSION_NUM / 2,
};

enum{
	NOTICEING = 0x1,
	QUERYING = 0x1,
	CAN_QUERY = 0x2,
	UPDATEING = 0x4
};

struct shadow_type_s{{//[ dlist_s + hash_key_s ] + shadow_type_s
//	char name[256];//标识一个镜像类型的
	version_info init_version;
	struct_all struct_types;
	double_list_s entity_table[ HASH_NUM ]; //double_list_s + hash_key_s + shadow_entity_s
	pthread_mutex_t entity_mutex;
	//下面可添加回调函数
};
typedef struct shadow_type_s shadow_type_s;
typedef struct shadow_type_s * shadow_type;

//数据包结构
#define PACKET_VERSION "M0"
enum{
	MOON_UPDATE = 0x1,
	MOON_UPDATE_ACK,
	MOON_QUERY,
	MOON_ALREADY_NEWEST,
	MOON_INIT,
	MOON_INIT_ACK
};
struct packet_head_s{
	char version[ 2 ]; // 2Byte:MX,X为版本，现在为0
	uint16_t cmd;//2Byte:指令
	uint32_t len; //4Byte:数据包总长度
	unsigned char protocol[ 8 ];
	unsigned char md5_sum[ 16 ];//16B:shadow类型与实体名合在一起的md5
}__attribute__((packed));
typedef struct packet_head_s packet_head_s;
typedef struct packet_head_s * packet_head;

//packet_head_s + packet_init_ack_s + ( struct all buf )
struct packet_init_ack_s{
	unsigned char init_version_md5[ 16 ];
}__attribute__((packed));
typedef struct packet_init_ack_s packet_init_ack_s;
typedef struct packet_init_ack_s * packet_init_ack;

typedef enum{
	MOON_UPDATE_SUCCESS = 0x0,
	MOON_VERSION_TOO_OLD = 0x1,
	MOON_UPDATE_HEAD_ERROR = 0x2,
	MOON_PACKET_ERROR = 0x3,
	MOON_INTERNAL_ERROR = 0x4
} moon_update_ack_e;

struct packet_update_ack_s{
	uint32_t status_code;
}__attribute__((packed));
typedef struct packet_update_ack_s packet_update_ack_s;
typedef struct packet_update_ack_s * packet_update_ack;

struct packet_query_s{
	unsigned char version_md5[ 16 ];
}__attribute__((packed));
typedef struct packet_query_s packet_query_s;
typedef struct packet_query_s * packet_query;

enum{
	HAS_DELS = 0x1,
	HAS_WRITES = 0x2,
	HAS_NEWS = 0x4,
	HAS_NEW_ID = 0x8
};

#define PAIRS_SIZE( p_change ) ( ( p_change )->num * sizeof( *( p_change )->pairs ) )
#define CHANG_ELEM_HEAD ( sizeof( uint64_t ) )
#define DELS_MAX_SIZE( p_del ) ( sizeof( uint64_t ) * ( ( p_del )->num ) )
#define WRITES_MAX_SIZE( p_write ) ( sizeof( uint64_t ) * ( ( p_write )->num * 2 ) )
#define NEWS_MAX_SIZE( p_new ) ( sizeof( uint64_t ) * ( ( p_new )->num * 3 ) )
struct packet_update_head_s{
	uint32_t from_version;
	uint32_t to_version;
	unsigned char from_md5[ 16 ];//修改的版本的md5
	unsigned char to_md5[ 16 ];//修改后版本的md5
	uint16_t info_bits;//指示del，write，new是否存在
}__attribute__((packed));
typedef struct packet_update_head_s packet_update_head_s;
typedef struct packet_update_head_s * packet_update_head;

typedef struct packet_change_s{ 
	int num;
	int len;
	char * data_ptr;
	union{
		addr_pair pairs;
		struct{
			unsigned long long id_array[ 3 ];
		} * p_ids;
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

static double_list_s shadow_head[ HASH_NUM ];//double_list_s + hash_key_s + shadow_type_s
static pthread_key_t shadow_key; 

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

void shadow_env_set( void * tp )
{
	pthread_setspecific( shadow_key, tp );
}

static inline void shadow_env_init()
{
	pthread_key_create( &shadow_key, NULL );
}

inline void * shadow_env_get()
{
	return pthread_getspecific( shadow_key );
}

static inline void version_surface_free( version_info p_version )
{	
	IF_FREE( p_version->order_id );
	IF_FREE( p_version->order_addr );
	if( p_version->p_buf_head != NULL ){
		free_total_packet( p_version->p_buf_head );
	}
	hash_free( data_to_list( p_version ) );
}

//shadow_type interface
static void notice_entity_closed( shadow_type p_type, shadow_entity p_entity )
{
	int is_del;

	pthread_mutex_lock( &p_type->entity_mutex );
	is_del = is_hash_really_del( p_entity );
	pthread_mutex_unlock( &p_type->entity_mutex );
	if( is_del ){
		entity_ref_dec( p_entity );
	}
}

//entity interface
static int is_entity_shadow( shadow_entity p_entity )
{
	return p_entity->is_shadow;
}

static void notice_version_closed( shadow_entity p_entity, version_info p_version )
{
	pthread_mutex_lock( &p_version->version_mutex );
	if( IS_ENTITY_CLOSED( p_entity ) ){
		MOON_PRINT_MAN( WARNNING, "version closed but entity already closed" );
	}else if( is_really_del( p_version ) ){
		if( ( p_version->cur_notice_version != NULL 
			&& p_version->version >= p_version->cur_notice_version->version ) ){
			MOON_PRINT_MAN( ERROR, "version closed when useing!" );
		}
		hash_del( data_to_list( p_version ) );
		p_entity->version_num--;
		version_ref_dec( p_version );
	}
	pthread_mutex_unlock( &p_version->version_mutex );
}

static void notice_point_closed( shadow_entity p_entity, linked_point p_point )
{
	int is_close = 0;

	pthread_mutex_lock( &p_entity->version_mutex );
	if( IS_ENTITY_CLOSED( p_entity ) ){
		MOON_PRINT_MAN( ERROR, "point closed but entity already closed" );
	}else if( is_really_del( p_point ) ){
		p_entity->points_num--;
		if( p_entity->next_notice_point == p_point ){
			p_entity->next_notice_point = dlist_next( p_entity->next_notice_point );
		}
		if( p_entity->is_shadow && p_entity->points_num == 0 ){
			p_entity->status = -1;
		}
		point_ref_dec( p_point );
		is_close = IS_ENTITY_CLOSED( p_entity );
	}
	pthread_mutex_unlock( &p_entity->version_mutex );
	if( is_close ){
		entity_close_func( p_entity );
	}
}

static int notice_user_num_inc( shadow_entity p_entity )
{
	int ret = -1;

	pthread_mutex_lock( &p_entity->version_mutex );
	if( !IS_ENTITY_CLOSED( &p_entity ) ){
		p_entity->total_user_num++;
		ret = 0;
	}
	pthread_mutex_unlock( &p_entity->version_mutex );
	return ret;
}

static int notice_user_num_dec( shadow_entity p_entity )
{
	int is_close = 0;

	pthread_mutex_lock( &p_entity->version_mutex );
	if( !IS_ENTITY_CLOSED( &p_entity ) ){
		p_entity->total_user_num--;
		is_close = IS_ENTITY_CLOSED( p_entity );
	}
	pthread_mutex_unlock( &p_entity->version_mutex );
	if( is_close ){
		entity_close_func( p_entity );
	}
	return 0;
}

static inline int _add_version_to_entity( shadow_entity p_entity, char * key, version_info p_version, int max_num, version_info * pp_del_version )
{
	version_info p_tmp;

	if( key != NULL ){
		set_key_of_value( p_version, key );
		if( ( p_tmp = list_to_data( hash_search2( p_entity->version_table
							, key, data_to_list( p_version ) ) ) ) == NULL ){
			MOON_PRINT_MAN( ERROR, "add version table error!" );
			return -1;
		}
		if( p_tmp != p_version ){
			MOON_PRINT_MAN( ERROR, "two version have the same md5!" );
			//覆盖前一个
			hash_insert( p_tmp, data_to_list( p_version ) );
		}
	}
	p_entity->version_num++;
	p_entity->version_alive_num++;
	p_entity->version_head = dlist_insert( p_entity->version_head, p_version );
	if( p_entity->version_alive_num > max_num ){
		p_entity->version_alive_num--;
		p_tmp = p_entity->version_tail;
		p_version->version_tail = dlist_prev( p_tmp );
		if( p_tmp != p_entity->root_version
			&& ( p_entity->cur_notice_version == NULL //服务器端是递增的
				|| p_tmp->version < p_entity->cur_notice_version->version ) ){
			*pp_del_version = p_tmp;
		}
	}
	return 0;
}

static int notice_task( common_user_data_u p_user_data )
{
	shadow_entity p_entity;
	buf_head p_buf_head;
	linked_point p_point;

	p_buf_head = NULL;
	p_entity = ( shadow_entity )p_user_data[ 0 ].ptr;
	while( 1 ){
		pthread_mutex_lock( &p_entity->version_mutex );
		if( !IS_ENTITY_CLOSED( p_entity ) ){
			if( ( p_entity->cur_notice_version == p_entity->version_head 
					&& p_entity->next_notice_point == NULL ) 
				|| p_entity->points_num == 0 ){
				p_entity->cur_notice_version = p_entity->version_head;
				if( p_buf_head != NULL ){
					free_total_packet( p_buf_head );
					p_buf_head = NULL;
				}
			}else{
				if( p_entity->next_notice_point == NULL ){
					p_entity->cur_notice_version = dlist_prev( p_entity->cur_notice_version );
					if( p_entity->cur_notice_version->version + RESET_MIN_LATER_VERSION_NUM 
							<= p_entity->version_head->version ){
						p_entity->cur_notice_version = p_entity->version_head;
					}
					p_entity->next_notice_point = list_to_data( p_entity->points.next );
					if( p_buf_head != NULL ){
						free_total_packet( p_buf_head );
						p_buf_head = NULL;
					}
				}
				p_point = p_entity->next_notice_point;
				point_ref_inc( p_point );
				p_entity->next_notice_point = dlist_next( p_entity->next_notice_point );
				if( p_buf_head == NULL ){
					p_buf_head = dump_packet( p_entity->cur_notice_version->p_buf_head );
				}
			}
		}
		pthread_mutex_unlock( &p_entity->version_mutex );
		if( p_buf_head == NULL ){
			break;
		}

	}
}

//ret: moon_update_ack_e
static int server_add_version_to_entity( shadow_entity p_entity, version_info p_base_version, version_info p_version )
{
	version_info p_tmp = NULL;
	int error_code, start_notice;
	char md5_buf[ 2 * sizeof( p_version->md5_sum ) + 1 ];

	sprint_binary( md5_buf, p_base_version->md5_sum, sizeof( p_base_version->md5_sum ) );
	error_code = MOON_INTERNAL_ERROR;
	start_notice = 0;
	pthread_mutex_lock( &p_entity->version_mutex );
	if( !IS_ENTITY_CLOSED( p_entity ) ){
		if( p_entity->version_head != p_base_version ){
			MOON_PRINT_MAN( WARNNING, "version too old to update!" );
			*error_code = MOON_VERSION_TOO_OLD;
		}else if( _add_version_to_entity( p_entity, md5_buf, p_version, SERVER_MAX_VERSION_NUM, &tmp ) < 0 ){
			MOON_PRINT_MAN( ERROR, "add version error!" );
			*error_code = MOON_INTERNAL_ERROR;
		}else{
			//通知开启notice任务
			*error_code = MOON_UPDATE_SUCCESS;
			start_notice = p_entity->notice_status & NOTICEING;
			p_entity->notice_status |= NOTICEING;
			start_notice ^= NOTICEING;
		}
	}
	pthread_mutex_unlock( &p_entity->version_mutex );
	if( p_tmp != NULL ){
		set_version_status_old( p_tmp );
	}
	if( start_notice == NOTICEING ){//start notice
		
	}
	return error_code;
}

static int add_point_to_entity( shadow_entity p_entity, linked_point p_point )
{
	int ret = -1;

	pthread_mutex_lock( &p_entity->version_mutex );
	if( !IS_ENTITY_CLOSED( p_entity ) ){
		if( is_point_in_entity( p_point ) ){
			MOON_PRINT_MAN( ERROR, "point now already in entity!" );
		}else{
			dlist_append( list_to_data( p_entity->points.next ), p_point );
			if( p_entity->points_num == 0 && p_entity->is_shadow != 0 ){
				p_entity->status = 0;
			}
			p_entity->points_num++;
			ret = 0;
		}
	}
	pthread_mutex_unlock( &p_entity->version_mutex );
	return ret;
}

static void close_entity( shadow_entity p_entity )
{
	int is_close = 0;

	pthread_mutex_lock( &p_entity->version_mutex );
	if( !IS_ENTITY_CLOSED( p_entity ) ){
		p_entity->status = -1;
		is_close = IS_ENTITY_CLOSED( p_entity );
	}
	pthread_mutex_unlock( &p_entity->version_mutex );
	if( is_close ){
		entity_close_func( p_entity );
	}
}

static update_packet malloc_uppac( unsigned long long id, unsigned version, buf_head p_buf_head )
{
	update_packet p_uppac = NULL;

	if( ( p_uppac = dlist_malloc( sizeof( *p_uppac ) ) ) == NULL ){
		goto back;
	}
	p_uppac->id = id;
	p_uppac->id = version;
	p_uppac->ptr = p_buf_head;
	return p_uppac;
back:
	return p_uppac;
}

static void free_uppac( update_packet p_uppac )
{
	free_total_packet( p_uppac->p_buf_head );
	dlist_free( p_uppac );
}

static inline update_packet get_tail_packet( update_packet * pp_up_head
		, update_packet * pp_up_tail, int * p_num )
{
	update_packet p_uppac = NULL;

	if( *pp_up_tail != NULL ){
		*p_num--;
		p_uppac = *pp_up_tail;
		*pp_up_tail = dlist_prev( *pp_up_tail );
		if( *pp_up_tail == NULL ){
			*pp_up_head = NULL;
		}
		dlist_del( p_uppac );
	}
	return p_uppac;
}

// version < old_value 的都删去
static inline update_packet del_old_packet( update_packet * pp_up_head
		, update_packet * pp_up_tail, int * p_num, int old_value )
{
	update_packet p_uppac;

	p_uppac = *pp_up_tail;
	if( p_uppac != NULL ){
		for( ; p_uppac != NULL && p_uppac->version < old_value
				; p_uppac = dlist_prev( p_uppac ) ){
			*p_num--;
		}
		*pp_up_tail = p_uppac;
		if( p_uppac == NULL ){
			p_uppac = *pp_up_head;
			*pp_up_head = NULL;
		}else{
			p_uppac = dlist_next( p_uppac );
			if( p_uppac != NULL ){
				dlist_cut( p_uppac );
			}
		}
	}
	return p_uppac;
}

static inline update_packet del_all_packet( update_packet * pp_up_head
		, update_packet * pp_up_tail, int * p_num )
{
	update_packet p_uppac;

	p_uppac = *pp_up_head;
	*pp_up_head = *pp_up_tail = NULL;
	*p_num = 0;
	return p_uppac;
}

static inline update_packet insert_new_packet( update_packet * pp_up_head
		, update_packet * pp_up_tail, int * p_num, update_packet p_new_uppac, int max_num )
{
	update_packet p_uppac = NULL;

	*p_num++;
	*pp_up_head = dlist_insert( *pp_up_head, p_new_uppac );
	if( *pp_up_tail == NULL ){
		*pp_up_tail = *pp_up_head;
	}
	//是否超过容量限制	
	if( *p_num >= CLIENT_MAX_VERSION_NUM ){
		MOON_PRINT_MAN( WARNNING, "update packet comeing too quick!" );
		p_uppac = get_tail_packet( pp_up_head, pp_up_tail, p_num );
	}
	return p_uppac;
}

#define CAN_START_UPDATE( p_entity ) ( p_entity->up_tail != NULL\
 && p_entity->up_tail->version == p_entity->version_head->version )

#define CAN_START_QUERY( p_entity ) ( ( p_entity->query_status & CAN_QUERY ) == CAN_QUERY\
 || p_entity->up_tail != NULL ) 

//ret: == QUERYING 启动query，== UPDATEING 启动update, == 0 不起动, < 0 error
static int entity_coming_update( shadow_entity p_entity, buf_head p_buf_head )
{
	int ret_code, len; 
	version_info p_version;
	update_packet p_uppac, p_del_uppacs, p_new_uppac;
	packet_head_s head;
	packet_update_headi_s update_head;
	char * buf;

	buf = NULL;
	len = ret_code = 0;
	p_del_uppacs = NULL;
	begin_travel_packet( p_buf_head );
	if( get_next_buf( p_buf_head, &buf, &len ) < sizeof( *p_head ) ){
		MOON_PRINT_MAN( ERROR, "get buf error!" );
		goto error;
	}
	head = *( packet_head )buf;
	if( unpack_packet_head( &head ) < 0 
		|| head.cmd != MOON_UPDATE
		|| memcmp( head.md5_sum, p_entity->md5_sum, sizeof( head.md5_sum ) ) != 0 
		|| len < head.len + sizeof( head )
		|| head.len <= sizeof( update_head ) ){
		MOON_PRINT_MAN( ERROR, "unpacket packet head error!" );
		goto error;
	}
	update_head = *( packet_update_head )( buf + sizeof( head ) );
	if( unpack_update_packet_head( &update_head ) < 0 ){
		MOON_PRINT_MAN( ERROR, "unpack packet error!" );
		goto error;
	}
	p_new_uppac = malloc_uppac( 0, p_up_head->from_version, p_buf_head );
	if( p_new_uppac == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc version uppac error!" );
		goto error;
	}
	pthread_mutex_lock( &p_entity->version_mutex );
	p_version = p_entity->version_head;
	if( !IS_ENTITY_CLOSED( p_entity ) && is_entity_shadow( p_entity ) ){
		p_entity->packet_id++;
		p_new_uppac->id = p_entity->packet_id;
		//正常情况下版本必须是递增的
		if(  p_up_head->from_version <= p_entity->last_bigest_version ){//error
			//此时删去所有包
			MOON_PRINT_MAN( ERROR, "version back!" );
			p_del_uppacs = del_all_packet( &p_entity->up_head, &p_entity->up_tail, &p_entity->packets_num );
			if( p_up_head->from_version < p_version->version ){
				p_entity->query_status |= CAN_QUERY;
			}
		}
		p_entity->last_bigest_version = p_up_head->from_version;
		//insert
		if( p_up_head->from_version >= p_version->version ){
			if( ( p_uppac = insert_new_packet( &p_entity->up_head, &p_entity->up_tail
					, &p_entity->packets_num, p_new_uppac, CLIENT_MAX_VERSION_NUM  ) ) != NULL ){
				p_del_uppacs = dlist_insert( p_del_uppacs, p_uppac );
			}
		}else{
			MOON_PRINT_MAN( WARNNING, "coming a late update packet!" );
			p_del_uppacs = dlist_insert( p_del_uppacs, p_new_uppac );
		}
		//看是否启动update task 或 query task
		if( ( p_entity->query_status & UPDATEING ) == 0 ){
			if( CAN_START_UPDATE( p_entity ) ){//启动update
				ret_code = UPDATEING;
				p_entity->query_status |= UPDATEING;
			}else if( CAN_START_QUERY( p_entity )
					&& ( p_entity->query_status & QUERYING ) == 0 ){
				ret_code = QUERYING;
				p_entity->query_status |= QUERYING;
			}
		}
	}
	pthread_mutex_unlock( &p_entity->version_mutex );
	while( p_del_uppacs != NULL ){
		p_uppac = p_del_uppacs;
		p_del_uppacs = dlist_next( p_del_uppacs );
		free_uppac( p_del_uppacs );
	}
	return ret_code;
error:
	free_total_packet( p_buf_head );
	return -1;
}

static int update_loop( shadow_entity p_entity )
{
	int ret_code, len, add_ok;
	version_info p_new_version, p_base_version, p_tmp;
	char * buf;
	packet_head_s head;
	update_packet p_uppac, p_del_uppacs;

	while( 1 ){
		len = add_ok = 0;
		ret_code = 0;
		buf = NULL;
		p_tmp = NULL;
		p_del_uppacs = NULL;
		pthread_mutex_lock( &p_entity->version_mutex );
		if( !IS_ENTITY_CLOSED( p_entity ) ){
			p_base_version = p_entity->version_head;
			if( CAN_START_UPDATE( p_entity ) ){
				p_uppac = get_tail_packet( &p_entity->up_head
						, &p_entity->up_tail, &p_entity->packets_num );
				version_ref_inc( p_base_version );
				ret_code = UPDATEING;
			}else{
				p_entity->status &= !UPDATEING;
				if( CAN_START_QUERY( p_entity ) 
					&& ( p_entity->query_status & QUERYING ) == 0 ){
					ret_code = QUERYING;
					p_entity->query_status |= QUERYING;
				}
			}
		}
		pthread_mutex_unlock( &p_entity->version_mutex );
		if( ret_code != UPDATEING ){
			break;
		}
		begin_travel_packet( p_uppac->p_buf_head );
		get_next_buf( p_uppac->p_buf_head, &buf, &len );
		head = *( packet_head )buf;
		unpack_packet_head( &head );
		if( _create_new_version( p_base_version, &p_new_version, buf + sizeof( head ), head.len ) < 0 ){
			MOON_PRINT_MAN( ERROR, "create new version error!" );
			goto version_error;
		}
		pthread_mutex_lock( &p_entity->version_mutex );
		if( !IS_ENTITY_CLOSED( p_entity )
			&& p_entity->version_head == p_base_version ){
			if( _add_version_to_entity( p_entity, p_new_version, NULL, CLIENT_MAX_VERSION_NUM, &p_tmp ) < 0 ){
				MOON_PRINT_MAN( ERROR, "add version error!" );
			}else{
				p_del_uppacs = del_old_packet( &p_entity->up_head, &p_entity->up_tail
						, &p_entity->packets_num, p_new_version->version );
				add_ok = 1;
			}
		}
		pthread_mutex_unlock( &p_entity->version_mutex );
		if( p_tmp != NULL ){
			set_version_status_old( p_tmp );
		}
		while( p_del_uppacs != NULL ){
			p_uppac = p_del_uppacs;
			p_del_uppacs = dlist_next( p_del_uppacs );
			free_uppac( p_del_uppacs );
		}
		if( add_ok == 0 ){
			set_version_status_old( p_new_version );
			version_ref_dec( p_new_version );
		}
version_error:
		free_uppac( p_uppac );
		version_ref_dec( p_base_version );
	}
	return ret_code;
}

//ret: < 0 error, >= 0 ok
static int send_query( linked_point p_point )
{
	int error;
	unsigned long long last_up_id;
	version_info p_base_version, p_root_version;
	buf_head p_buf_head;
	shadow_entity p_entity;

	p_entity = p_point->p_entity;
	p_root_version = p_base_version = NULL;
	error = -1;
	pthread_mutex_lock( &p_entity->version_mutex );
	if( !IS_ENTITY_CLOSED( p_entity ) ){
		p_base_version = p_entity->version_head;
		version_ref_inc( p_base_version );
		p_root_version = p_entity->root_version;
		version_ref_inc( p_root_version );
		p_entity->query_status &= !CAN_QUERY;
		last_up_id = p_entity->packet_id;
	}
	pthread_mutex_unlock( &p_entity->version_mutex );
	if( p_base_version == NULL ){
		goto back;
	}
	if( inc_version_user_num( p_base_version ) < 0 ){
		MOON_PRINT_MAN( ERROR, "version closed when query!" );
		goto version_error;
	}
	//创建query packet
	p_buf_head = create_query_packet( p_base_version );
	if( p_buf_head == NULL ){
		MOON_PRINT_MAN( ERROR, "create query packet error!" );
		goto create_packet_error;
	}
	//send,todo:以后放在can_send中
	if( p_point->stream_i.send_packet(  p_point->stream_i.user_data, p_buf_head, 0 ) < 0 ){
		MOON_PRINT_MAN( ERROR, "send packet error!" );
		goto send_error;
	}
	version_ref_inc( p_base_version );
	version_ref_inc( p_root_version );
	p_point->user_i.inter_user_data[ 0 ].ull_num = last_up_id;
	p_point->user_i.inter_user_data[ 1 ].ptr = p_base_version;
	p_point->user_i.inter_user_data[ 2 ].ptr = p_root_version;
	p_point->user_i.can_recv_func = recv_query_ack;
	p_point->user_i.close_func = querying_close;
	//set can_send
	if( p_point->stream_i.recv_next_packet( p_point->stream_i.user_data, 10000 ) < 0 ){
		MOON_PRINT_MAN( ERROR, "set recv query ack error!" );
		goto create_packet_error;
	}
	goto create_packet_error;

send_error:
	free_total_packet( p_buf_head );
create_packet_error:
	dec_version_user_num( p_base_version );
version_error:
	version_ref_dec( p_base_version );
	version_ref_dec( p_root_version );
back:
	return error;
}

static int querying_close( linked_point p_point )
{
	shadow_entity p_entity;
	version_info p_version;

	pthread_mutex_lock( &p_entity->version_mutex );
	if( !IS_ENTITY_CLOSED( p_entity ) ){
		p_entity->query_status &= !QUERYING;
	}
	pthread_mutex_unlock( &p_entity->version_mutex );
	p_version = ( version_info )p_point->user_i.inter_user_data[ 1 ].ptr;
	version_ref_dec( p_version );
	p_version = ( version_info )p_point->user_i.inter_user_data[ 2 ].ptr;
	version_ref_dec( p_version );
	return 0;
}

//ret: < 0 error,  == 0 do noting, QUERYING 继续查询, UPDATEING 更新
static int _recv_query_ack( linked_point p_point, buf_head p_buf_head )
{
	char * buf;
	version_info p_base_version, p_new_version, p_tmp;
	version p_root_version, p_prev_version;;
	shadow_entity p_entity;
	packet_head_s head;
	packet_update_head_s update_head;
	update_packet p_del_uppacs;
	int ret, ret_code, len, is_newest, add_ok;
	unsigned long long last_up_id;

	ret_code = -1;
	buf = NULL;
	p_del_uppacs = NULL;
	p_tmp = p_new_version = NULL;
	is_newest = len = add_ok = 0;
	p_entity = p_point->p_entity;
	last_up_id = p_point->user_i.inter_user_data[ 0 ].ull_num;
	p_base_version = ( version_info )p_point->user_i.inter_user_data[ 1 ].ptr;
	p_root_version = ( version_info )p_point->user_i.inter_user_data[ 2 ].ptr;
	memset( &p_point->user_i, 0, sizeof( p_point->user_i ) );

	begin_travel_packet( p_buf_head );
	if( get_next_buf( p_buf_head, &buf, &len ) < sizeof( head ) ){
		MOON_PRINT_MAN( ERROR, "get buf error!" );
		goto packet_error;
	}
	head = *( packet_head )buf;
	if( unpack_packet_head( &head ) < 0 
		|| memcmp( head.md5_sum, p_entity->md5_sum, sizeof( head.md5_sum ) ) != 0 
		|| len < head.len + sizeof( head ) ){
		MOON_PRINT_MAN( ERROR, "unpacket packet head error!" );
		goto packet_error;
	}
	switch( head.cmd ){
	case MOON_ALREADY_NEWEST:
		is_newest = 1;
		break;
	case MOON_UPDATE:
		if( head.len < sizeof( update_packet ) ){
			goto packet_error;
		}
		update_head = *( packet_update_head )( buf + sizeof( head ) );
		if( unpack_update_packet_head( &update_head ) < 0 ){
			MOON_PRINT_MAN( ERROR, "unpack update hhead error!" );
			goto packet_error;
		}
		p_prev_version = p_base_version;
		if( memcmp( p_base_version->md5_sum
					, update_head.from_md5, sizeof( update_head.from_md5 ) ) != 0 ){
			p_prev_version = p_root_version;
		}
		if( _create_new_version( p_prev_version, &p_new_version, buf + sizeof( head ), head.len ) < 0 ){
			MOON_PRINT_MAN( ERROR, "create update version error!" );
			goto packet_error;		
		}
		break;
	default:
		goto packet_error;
	}
	ret_code = 0;
packet_error:
	pthread_mutex_lock( &p_entity->version_mutex );
	if( !IS_ENTITY_CLOSED( p_entity ) ){
		if( p_new_version != NULL ){
			if( p_new_version->version > p_entity->version_head->version ){//insert
				ret = _add_version_to_entity( p_entity, NULL, p_new_version, CLIENT_MAX_VERSION_NUM, &p_tmp );
				if( ret < 0 ){
					//never here
					MOON_PRINT_MAN( ERROR, "add version error!" );
				}else{
					add_ok = 1;
					p_del_uppacs = del_old_packet( &p_entity->up_head, &p_entity->up_tail
							, &p_entity->packets_num, p_new_version->version );
				}
			}else if( p_new_version->version <= p_base_version->version ){// version back error
				MOON_PRINT_MAN( ERROR, "version back!" );
				//del all packets
				ret = _add_version_to_entity( p_entity, NULL, p_new_version, CLIENT_MAX_VERSION_NUM, &p_tmp );
				if( ret < 0 ){
					//never here
					MOON_PRINT_MAN( ERROR, "add version error!" );
				}else{
					add_ok = 1;
					p_del_uppacs = del_all_packet( &p_entity->up_head
							, &p_entity->up_tail, &p_entity->packets_num );
				}
			}else{//drop
				is_newest = 1;
			}
		}else if( is_newest != 0 
			&& last_up_id == p_entity->packet_id 
			&& p_entity->up_tail != NULL ){//error
			MOON_PRINT_MAN( ERROR, "version back!" );
			p_del_uppacs = del_all_packet( &p_entity->up_head, &p_entity->up_tail, &p_entity->packets_num );
		}
		p_entity->query_status &= !QUERYING;
		if( ret_code >= 0 && ( p_entity->query_info & UPDATEING ) == 0 ){
			if( CAN_START_UPDATE( p_entity ) ){
				ret_code = UPDATEING;
				p_entity->query_status |= UPDATEING;
			}else if( CAN_START_QUERY( p_entity )
				|| ( is_newest == 0 && last_up_id == p_entity->packet_id ) ){
				ret_code = QUERYING;
				p_entity->query_status |= QUERYING;
			}
		}
	}
	pthread_mutex_unlock( &p_entity->version_mutex );
	while( p_del_uppacs != NULL ){
		p_uppac = p_del_uppacs;
		p_del_uppacs = dlist_next( p_del_uppacs );
		free_uppac( p_del_uppacs );
	}
	if( p_tmp != NULL ){
		set_version_status_old( p_tmp );
	}
	if( add_ok == 0 && p_new_version != NULL ){
		set_version_status_old( p_new_version );
		version_ref_dec( p_new_version );
	}
	version_ref_dec( p_base_version );
	version_ref_dec( p_root_version );
	free_total_packet( p_buf_head );
	return ret_code;
}

static int recv_query_ack( linked_point p_point, buf_head p_buf_head )
{
	int ret_code;

	ret_code = _recv_query_ack( p_point, p_buf_head );
	if( ret_code < 0 ){
		return ret_code;
	}
	if( ret_code == UPDATEING ){
		ret_code = update_loop( p_point->p_entity );
	}
	if( ret_code == QUERYING ){
		if( ( ret_code = send_query( p_point ) ) < 0 ){
			MOON_PRINT_MAN( ERROR, "send query error!" );
		}
	}
	return ret_code;
}

static void entity_close_func( shadow_entity p_entity )
{
	version_info p_version;
	linked_point p_point;

	//entity close后就没有其他线程可以放问同步结构了，所以不用锁
	for( p_version = p_entity->version_head; p_version != NULL
			; p_version = dlist_next( p_version ) ){
		set_version_status_old( p_version );
		version_ref_dec( p_version );
	}
	set_version_status_old( p_entity->root_version );
	version_ref_dec( p_entity->root_version );
	for( p_point = list_to_data( p_entity->points.next ); p_point != NULL
			; p_point = dlist_next( p_point ) ){
		close_point( p_point );
		point_ref_dec( p_point );
	}

	//通知shadow_type hash 表释放entity
	notice_entity_closed( p_entity->type, p_entity );
}

//version interface
static void set_version_status_old( version_info p_version )
{
	int tmp;

	do{
		tmp = p_version->status;
		if( tmp == VERSION_STATUS_NORMAL ){
			if( __sync_bool_compare_and_swap( &p_version->status, tmp, STATUS_CLOSED ) ){
				break;
			}
		}else if( ( tmp & STATUS_MASK ) == VERSION_STATUS_NORMAL ){
			if( __sync_bool_compare_and_swap( &p_version->status, tmp
					, ( tmp & ~STATUS_MASK ) | VERSION_STATUS_OLD ) ){
				break;
			}
		}
	}while( ( tmp & STATUS_MASK ) == VERSION_STATUS_NORMAL );
	if( tmp == VERSION_STATUS_NORMAL ){
		version_close_func( p_version );
	}
}

static int inc_version_user_num( version_info p_version )
{
	if( useing_ref_inc( &p_version->status ) < 0 ){
		return -1;
	}
	version_user_num_inc( p_version );
	return 0;
}

static int dec_version_user_num( version_info p_version )
{
	version_user_num_dec( p_version );
	do{
		tmp = p_version->status;
		if( tmp == ( VERSION_STATUS_OLD | USEING_REF_UNIT ) ){
			if( __sync_bool_compare_and_swap( &p_version->status, tmp, STATUS_CLOSED ) ){
				break;
			}
		}else{
			if( __sync_bool_compare_and_swap( &p_version->status, tmp, tmp - USEING_REF_UNIT ) ){
				break;
			}
		}
	}while( 1 );
	if( tmp == ( VERSION_STATUS_OLD | USEING_REF_UNIT )
		|| tmp == ( VERSION_STATUS_CLOSED | USEING_REF_UNIT ) ){
		version_close_func( p_version );
	}
}

static inline void version_close_func( version_info p_version )
{
	int i;

	for( i = 0; i < p_version->block_num; i++ ){
		mem_block_ref_dec( p_version->order_id[ i ].addr );
	}
	notice_version_closed( p_version->entity, p_version );
	entity_ref_dec( p_version->entity );
}

static inline mem_block mem_block_surface_dump( mem_block p_block )
{
	mem_block p_dst_block;

	p_dst_block = ( mem_block )calloc( 1, sizeof( mem_block_s ) + p_block->data_len );
	if( p_dst_block == NULL ){
		return NULL;
	}
	p_dst_block->data = ( char * )( p_dst_block + 1 );
	if( p_block->is_root > 0 ){
		p_dst_block->is_root = 2;
	}
	if( p_block->p_original_block == NULL ){//root
		mem_block_ref_inc( p_block );
		p_dst_block->p_original_block = p_block;
	}else{//副本
		mem_block_ref_inc( p_block->p_original_block );
		p_dst_block->p_original_block = p_block->p_original_block;
	}
	p_dst_block->virtual_addr = p_block->virtual_addr;
	p_dst_block->version = p_block->version;
	p_dst_block->type_id = p_block->type_id;
	p_dst_block->type_num = p_block->type_num;
	p_dst_block->data_len = p_block->data_len;
	p_dst_block->id = p_block->id;
	p_dst_block->ref_num = 1;
	return p_dst_block;
}

static inline int create_new_block( struct_all p_all, int type_id, int type_num )
{
	int len;
	mem_block p_block;

	len = get_struct_len( p_all, type_id );
	len *= type_num;
	if( len <= 0 ){
		MOON_PRINT_MAN( ERROR, "a type of struct error!" );
		return NULL;
	}
	p_block = ( mem_block )calloc( sizeof( mem_block_s ) + len, 1 );
	if( p_block == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		return NULL;
	}
	p_block->virtual_addr = p_block + 1;
	p_block->type_id = type_id;
	p_block->type_num = type_num;
	p_block->data_len = len;
	p_block->data = p_block->virtual_addr;
	p_block->ref_num = 1;
	return p_block;
}

static inline version_info version_surface_malloc( int block_num )
{
	version_info p_new_version = NULL;

	p_new_version = list_to_data( hash_malloc( sizeof( double_list_s ) + sizeof( *p_new_version ) ) );
	if( p_new_version == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		goto version_error;
	}
	p_new_version->block_num = block_num;
	if( ( block_num > 0 
		&& ( p_new_version->order_id = ( addr_pair )calloc( sizeof( addr_pair_s ), block_num ) ) == NULL ) ){
		MOON_PRINT_MAN( ERROR, "malloc error!");
		goto id_error;
	}
	p_new_version->ref_num = 1;
	return p_new_version;
id_error:
	hash_free( data_to_list( p_new_version ) );
version_error:
	return NULL;
}

static inline version_info version_dump( version_info  p_version )
{
	version_info p_new_version;
	mem_block p_block, p_new_block;
	addr_pair p_pair;
	int i;

	if( ( p_new_version = version_surface_malloc( p_version->block_num ) ) == NULL ){
		MOON_PRINT_MAN( ERROR, "surface dump version error!" );
		goto version_error;
	}
	for( i = 0; i < p_version->block_num; i++ ){
		p_block = p_version->order_id[ i ].addr;
		p_pair = &p_new_version->order_id[ i ];
		if( ( p_new_block = mem_block_surface_dump( p_block ) ) == NULL ){
			MOON_PRINT_MAN( ERROR, "dump block error!");
			goto id_error;
		}
		p_pair->id = p_block->id;
		p_pair->addr = p_new_block;
		memcpy( p_new_block->data, p_block->data, p_block->data_len );
		memcpy( p_new_block->md5_sum, p_block->md5_sum, sizeof( p_new_block->md5_sum ) );
	}
	if( ( p_new_version->order_addr = sort_addr_id( p_new_version->order_id
			, p_new_version->block_num ) ) == NULL ){
		MOON_PRINT_MAN( ERROR, "get order addr array error!" );
		goto id_error;
	}
	return p_new_version;
id_error:
	p_version->status = STATUS_CLOSED;
	for( i--; i > 0; i-- ){
		mem_block_ref_dec( p_new_version->order_id[ i ].addr );
	}
	version_ref_dec( p_new_version );
version_error:
	return NULL;
}

static inline void entity_md5( unsigned char *md5, char * type, char * xid )
{
	MD5_CTX md5_ctx;

	MD5_Init( &md5_ctx );
	MD5_Update( &md5_ctx, type, strlen( type ) );
	MD5_Update( &md5_ctx, xid, strlen( xid ) );
	MD5_Final( md5, &md5_ctx );
}

/*
//暂时不写从序列化中恢复的情况和从网络端产生的影子版本，现在仅仅是为了测试而已
int shadow_entity_add( char * type, char * xid, int is_shadow )
{
	shadow_type p_type;
	shadow_entity_s entity;
	shadow_entity p_entity;
	version_info p_version;
	unsigned char buf[ sizeof( entity.md5_sum ) * 2 + 1 ];
	
	if( type == NULL || xid == NULL ){
		return -1;
	}
	if( ( p_type = ( shadow_type )hash_search( shadow_head, type, 0 ) ) == NULL ){
		MOON_PRINT_MAN( ERROR, "no such shadow type!" );
		return -1;
	}
	memset( &entity, 0, sizeof( entity ) );
	entity_md5( entity.md5_sum, type, xid );
	sprint_binary( buf, entity.md5_sum, sizeof( entity_md5 ) );
	pthread_mutex_lock( &p_type->entity_mutex );
	if( hash_search( p_type->entity_table, buf, 0 ) != NULL ){
		MOON_PRINT_MAN( ERROR, "already has such entity:%s", xid );
		pthread_mutex_unlock( &p_type->entity_mutex );
		return -1;
	}
	pthread_mutex_unlock( &p_type->entity_mutex );
	if( ( p_version = version_dump( ( version_info )( p_type->init_version + 1 ) ) ) 
				== NULL ){
		MOON_PRINT_MAN( ERROR, "dump version error!" );
		return -1;
	}
	pthread_mutex_init( &entity.version_mutex, NULL );
	entity.shadow_type = p_type;
	entity.remote_all = dump_struct_all( p_type->struct_types );
	entity.is_shadow = is_shadow;

	pthread_mutex_lock( &p_type->entity_mutex );
	if( ( p_entity = ( shadow_entity )hash_search( p_type->entity_table, buf
			, sizeof( *p_entity ) ) ) == NULL || p_entity->version_head != NULL ){
		MOON_PRINT_MAN( ERROR, "create a new shadow entity error!" );
		version_free( p_version, DEL_ORDER_IDS );
		pthread_mutex_unlock( &p_type->entity_mutex );
		return -1;
	}
	*p_entity = entity;
	p_version->entity = p_entity;
	p_entity->version_head = dlist_insert( p_entity->version_head, p_version );
	pthread_mutex_unlock( &p_type->entity_mutex );
	return 0;
}

*/

void * shadow_runtime( char * type, void * p_data, int len, char * opt )
{
	addr_pair search_addr_id( addr_pair in, unsigned long long id, unsigned long num );

	thread_private tp_data = NULL;
	addr_pair cur_pair;
	mem_block p_block, copy_block;
	unsigned long long offset;

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
	cur_pair = avl_search( tp_data->avl, ( uintptr_t )p_data );
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
				copy_block->data = ( char * )( copy_block + 1 );
				pthread_mutex_lock( &tp_data->avl_mutex );
				cur_pair = avl_add( &tp_data->avl, ( uintptr_t )copy_block->virtual_addr );
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
			default:
				MOON_PRINT_MAN( WARNNING, "unknown opreation!");
				return p_data;
			}
		}else{
			if( opt[ 0 ] != 'c' ){
				MOON_PRINT_MAN( ERROR, "address is error:%p ", p_data );
			}
			return p_data;
		}
	}else{
		MOON_PRINT_MAN( ERROR, "address is error:%p ", p_data );
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
	p_block->data = p_block->virtual_addr;

	pthread_mutex_lock( &tp->avl_mutex );
	pair = avl_add( &tp->avl, ( uintptr_t )p_block->virtual_addr );
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
	p_pair = avl_search( tp->avl, ( uintptr_t )addr);
	pthread_mutex_unlock( &tp->avl_mutex);
	if( p_pair != NULL && p_pair->virtual_addr == ( uintptr_t )addr ){
		if( p_pair->addr->id == 0 ){//新建的
			pthread_mutex_lock( &tp->avl_mutex );
			pair = avl_del( &tp->avl, ( uintptr_t )addr );
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
	p_pair = avl_add( &tp->avl, ( uintptr_t )addr );
	pthread_mutex_unlock( &tp->avl_mutex );
	if( p_pair == NULL ){
		MOON_PRINT_MAN( ERROR, "insert avl error!" );
		return;
	}
	p_pair->addr = p_block;
	return;
}

static void entity_ref_dec( shadow_entity p_entity )
{
	int tmp;
	update_packet p_uppac, p_tmp_uppac;

	tmp = __sync_sub_and_fetch( &p_entity->ref_num, 1 );
	if( tmp == 0 ){
		if( !IS_ENTITY_CLOSED( p_entity ) ){
			MOON_PRINT_MAN( ERROR, "entity not closed when free!" );
		}
		free_struct_all( p_entity->remote_all );
		pthread_mutex_destroy( &p_entity->version_mutex );
		for( p_uppac = p_entity->up_head; p_uppac != NULL; ){
			p_tmp_uppac = p_uppac;
			p_uppac = dlist_next( p_uppac );
			free_uppac( p_tmp_uppac );
		}
		if( p_entity->init_ack_pac != NULL ){
			free_total_packet( p_entity->init_ack_pac );
		}
		hash_free( p_entity );
	}else if( tmp < 0 ){
		MOON_PRINT_MAN( ERROR, "entity ref num under overflow!" );
	}

}

static void entity_ref_inc( shadow_entity p_entity )
{
	int tmp;

	tmp = __sync_add_and_fetch( &p_entity->ref_num, 1 );
	if( tmp < 0 ){
		MOON_PRINT_MAN( ERROR, "entity ref num up overflow!" );
	}

}

static void version_user_num_inc( version_info p_version )
{
	int tmp;

	tmp = __sync_add_and_fetch( &p_version->user_num, 1 );
	if( tmp < 0 ){
		MOON_PRINT_MAN( ERROR, "version user num up overflow!" );
	}
}

static void version_user_num_dec( version_info p_version )
{
	int tmp;

	tmp = __sync_sub_and_fetch( &p_version->user_num, 1 );
	if( tmp < 0 ){
		MOON_PRINT_MAN( ERROR, "version user num under overflow!" );
	}
}

static void version_ref_inc( version_info p_version )
{
	int tmp;

	tmp = __sync_add_and_fetch( &p_version->ref_num, 1 );
	if( tmp < 0 ){
		MOON_PRINT_MAN( ERROR, "version ref num up overflow!" );
	}
}

static void version_ref_dec( version_info p_version )
{
	int tmp;

	tmp = __sync_sub_and_fetch( &p_version->ref_num, 1 );
	if( tmp == 0 ){
		if( !STATUS_LEGAL_CLOSED( p_version->status ) ){
			MOON_PRINT_MAN( ERROR, "status is inlegal when version free" );
		}
		if( p_version->user_num != 0 ){
			MOON_PRINT_MAN( ERROR, "user num is not zero when version free" );
		}
		version_surface_free( p_version );
	}else if( tmp < 0 ){
		MOON_PRINT_MAN( ERROR, "version ref num under overflow!" );
	}
}

static void mem_block_ref_inc( mem_block p_block )
{
	int tmp;

	__sync_add_and_fetch( &p_block->ref_num, 1 );
	if( tmp < 0 ){
		MOON_PRINT_MAN( ERROR, "memblock ref num up overflow!" );
	}
}

static void mem_block_ref_dec( mem_block p_block )
{
	int tmp;

	tmp = __sync_sub_and_fetch( &p_block->ref_num, 1 );
	if( tmp == 0 ){
		if( p_block->p_original_block != NULL ){
			mem_block_ref_dec( p_block->p_original_block );
		}
		free( p_block );
	}else if( tmp < 0 ){
		MOON_PRINT_MAN( ERROR, "memblock ref num under overflow!" );
	}
}

void * shadow_env_new( char *type, char * xid, unsigned long ver_num )
{
	shadow_type p_type;
	shadow_entity p_entity;
	version_info p_version;
	thread_private tp;
	unsigned char md5_sum[ sizeof( p_entity->md5_sum ) ];
	unsigned char buf[ sizeof( p_entity->md5_sum ) * 2 + 1 ];

	if( type == NULL || xid == NULL ){
		MOON_PRINT_MAN( ERROR, "input paraments error!" );
		goto error;
	}
	tp = ( thread_private )malloc( sizeof( *tp ) );
	if( tp == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		goto error;
	}
	if( pthread_mutex_init( &tp->avl_mutex, NULL ) != 0 ){
		MOON_PRINT_MAN( ERROR, "avl mutex init error!" );
		goto mutex_error;
	}
	p_type = ( shadow_type )hash_search( shadow_head, type, 0 );
	if( p_type == NULL ){
		MOON_PRINT_MAN( ERROR, "no such shadow type:%s", type );
		goto find_error;
	}
	entity_md5( md5_sum, type, xid ):
	sprint_binary( buf, md5_sum, sizeof( md5_sum ) );
	pthread_mutex_lock( &p_type->entity_mutex );
	p_entity = hash_search( p_type->entity_table, buf, 0 );
	if( p_entity != NULL ){
		entity_ref_inc( p_entity );
	}
	pthread_mutex_unlock( &p_type->entity_mutex );
	if( p_entity == NULL ){
		MOON_PRINT_MAN( ERROR, "no such entity:%s",  xid );
		goto find_error;
	}
	pthread_mutex_lock( &p_entity->version_mutex );
	if( !IS_ENTITY_CLOSED( p_entity ) ){	
		p_version = p_entity->version_head;
		if( p_version != NULL && ver_num == NEWEST_VERSION ){
			version_ref_inc( p_version );
		}else{
			for( ; p_version != NULL; p_version = dlist_next( p_version ) ){
				if( p_version->version != ver_num ){
					p_version = NULL;
				}else{
					version_ref_inc( p_version );
					break;
				}
			}
		}
	}
	pthread_mutex_unlock( &p_entity->version_mutex );
	if( p_version == NULL ){
		MOON_PRINT_MAN( ERROR, "can't find such version!" );
		goto version_error;
	}
	if( inc_version_user_num( p_version ) < 0 ){
		MOON_PRINT_MAN( ERROR, "inc version user num error!" );
		goto user_error;
	}
	if( notice_user_num_inc( p_entity ) < 0 ){
		MOON_PRINT_MAN( ERROR, "notice entity error!" );
		goto notice_error;
	}
	strncpy( tp->type, type, sizeof( tp->type ) );
	strncpy( tp->xid, xid, sizeof( tp->xid ) );
	tp->version = p_version;
	tp->avl = NULL;
	entity_ref_dec( p_entity );
	return tp;

notice_error:
	dec_version_user_num( p_version );
user_error:
	version_ref_dec( p_version );
version_error:
	entity_ref_dec( p_entity );
find_error:
	pthread_mutex_destroy( &tp->avl_mutex );
mutex_error:
	free( tp );
error:
	return NULL;
}

static inline int get_virtual_address_info( mem_block p_block, void *virtual_addr, unsigned * p_offset, void ** p_real_addr )
{
	int offset;

	offset = ( uintptr_t)virtual_addr - ( uintptr_t )p_block->virtual_addr;
	if( offset < 0 || offset >= p_block->data_len ){
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
		src = ( void * )( ( type * )( src ) + 1 );\
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

	ret = deflateInit2( &stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY );
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

static inline int get_blockbuf_len( mem_block p_block, struct_all p_all )
{
	int tmp1, tmp2, total_len;

	tmp1 = get_struct_len( p_all, p_block->type_id );
	tmp2 = get_struct_points_num( p_all, p_block->type_id );
 	total_len = p_block->type_num * ( tmp1 + tmp2 * ID_OFFSET_LEN );
	if( tmp1 <= 0 || tmp2 < 0 ){
		MOON_PRINT_MAN( ERROR, "can't find the block type!" );
		return -1;
	}
	return total_len;
}
//第一次遍历过程，证明所有id类型都是合法的，并且所有的write都有原始版本
int  pac1_func( addr_pair p_pair, packet p_pac )
{
	mem_block p_block;
	int total_len;
	packet_change p_del, p_write, p_new;

	p_del   = &p_pac->changes[ 0 ];
	p_write = &p_pac->changes[ 1 ];
	p_new   = &p_pac->changes[ 2 ];
	p_block = p_pair->addr;
 	total_len = get_blockbuf_len( p_block, p_pac->to_all );
	if( total_len <= 0 ){
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
		p_tmp_pair = avl_search( p_avl, ( uintptr_t )points[ j ] );
		if( p_tmp_pair != NULL && get_virtual_address_info( p_tmp_pair->addr
				, points[ j ], &offset, NULL ) >= 0 ){
			p_block = p_tmp_pair->addr;
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
		MOON_PRINT_MAN( WARNNING, "a point is not the range of any block address:%p", points[ j ] );			
		id_offset_write( &( ( packet_id_offset )id_offset )[ j ], 0, 0 );
	}
	return 0;
}

static inline int id_offset_to_points( void ** points, packet_id_offset p_id_offset, int num, block_copy p_copy, version_info p_version )
{
	int i, tmp;
	addr_pair p_pair;
	mem_block p_block;

	for( i = 0; i< num; i++ ){
		convert_uint64_t( &p_id_offset[ i ].id );
		convert_uint32_t( &p_id_offset[ i ].offset );
		if( p_id_offset[ i ].id == 0 && p_id_offset[ i ].offset == 0 ){
			points[ i ] = NULL;
			continue;
		}
		p_pair = search_addr_id( p_version->order_id
				, p_id_offset[ i ].id, p_version->block_num );
		if( p_pair == NULL ){
			MOON_PRINT_MAN( ERROR, "can't find this poins-id" );
			return -1;
		}
		p_block = p_pair->addr;
		tmp = get_target_offset( p_copy->src_type, p_copy->dst_type
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
	cur_type _tmp;\
	int _cur_type_len = ( sizeof( cur_type ) << 3 ) - 1;\
	int _ret;\
\
	_ret = -1;\
	if( *( p_len ) >= sizeof( cur_type ) ){\
		_p = ( cur_type * )*( ppc );\
		_tmp = *_p;\
		convert_##cur_type( &_tmp );\
		*( p_num ) += ( _tmp & ~( ( uint64_t )1 << _cur_type_len ) ) << ( prev_bit_len );\
		if( ( _tmp & ( ( uint64_t )1 << _cur_type_len ) ) > 0 ){\
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
		tmp2 = get_struct_points_num( p_copy->src_type, p_block->type_id );
		len = p_block->type_num * ( tmp1 + tmp2 * ID_OFFSET_LEN );
		p_copy->points = ( void ** )( pc + p_block->type_num * tmp1 );
		
		MD5( ( unsigned char * )pc, len, p_block->md5_sum );
		if( id_offset_to_points( p_copy->points, ( packet_id_offset )p_copy->points
				, p_block->type_num * tmp2, p_copy, p_version ) < 0 ){
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

static inline int packet_del_data( packet_change p_change )
{
	uint64_t prev_64, tmp_64;
	char * pc;
	int i;
	mem_block p_block;

	pc = ( char * )p_change->data_ptr;
	if( convert_to_moon_num( ( void ** )&pc, p_change->num, &p_change->len ) < 0 ){
		MOON_PRINT_MAN( ERROR, "convert to moon num error!" );
		return -1;
	}
	prev_64 = 0;;
	for( i = 0; i < p_change->num; i++ ){
		p_block = p_change->pairs[ i ].addr;
		tmp_64 = p_block->id - prev_64;
		prev_64 = p_block->id;
		if( convert_to_moon_num( ( void **)&pc, tmp_64, &p_change->len ) < 0  ){
			MOON_PRINT_MAN( ERROR, "convvert to moon num error!" );
			return -1;
		}
	}
	return 0;
}

static inline int packet_new_data( packet_change p_change )
{	
	char * pc;
	int i;
	mem_block p_block;
	
	pc = ( char * )p_change->data_ptr;
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
	return 0;
}

static int get_commit_packet( thread_private tp, char ** p_buf, int * p_len )
{
	packet_s pac;
	packet_change p_del, p_write, p_new;
	packet_data p_data;
	packet_data_head p_data_head;
	packet_update_head p_update_head;
	packet_head p_head;
	int i, j, k, tmp1, ret;
	mem_block p_block;
	version_info p_version;
	char *pc, *buf, *pkt_buf, *data_ptr, *prev_data_ptr;
	block_copy_s copy_block;	
	MD5_CTX md5_ctx;

	buf = NULL;
	prev_data = NULL;
	ret = -1;
	p_version = tp->version;
	memset( &pac, 0, sizeof( pac ) );
	pac.from_all = p_version->entity->shadow_type->struct_types;
	pac.to_all = p_version->entity->remote_all;
	pac.version = p_version;
	copy_block.flag = 0;
	copy_block.src_type = pac.from_all;
	copy_block.dst_type = pac.to_all;
	p_del = &pac.changes[ 0 ];
	p_write = &pac.changes[ 1 ];
	p_new = &pac.changes[ 2 ];
	p_data = &pac.data;
	p_data_head = &p_data.head
	p_update_head = &pac.update_head;	
	
	pthread_mutex_lock( &tp->avl_mutex );
	//第一次遍历avl
	if( avl_traver_first( tp->avl, pac1_func, &pac ) < 0 ){
		MOON_PRINT_MAN( ERROR, "traver avl first time error!" );
		goto back1;
	}
	if( p_write->num <= 0 ){
		MOON_PRINT_MAN( ERROR, "no blocks write");
		goto back1;
	}
	//这是一段不太好的代码，试图把临时内存分配在一块内存上
	tmp1 = 0;
	tmp1 += CHANG_ELEM_HEAD + MAx( PAIRS_SIZE( p_del ), DELS_MAX_SIZE( p_del ) );
	tmp1 += CHANG_ELEM_HEAD + MAx( PAIRS_SIZE( p_write ), WRITES_MAX_SIZE( p_write ) ); 
	tmp1 += PAIRS_SIZE( p_write );
	tmp1 += CHANG_ELEM_HEAD + MAX( PAIRS_SIZE( p_new ), NEWS_MAX_SIZE( p_new ) );
	tmp1 += ( pac.write_block_size + pac.new_block_size ) * 2;
	buf = malloc( tmp1 );
	if( buf == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		goto back1;
	}
	pc = buf;
	p_del->data_ptr = pc;
	pc += CHANG_ELEM_HEAD + MAX( PAIRS_SIZE( p_del ), DELS_MAX_SIZE( p_del ) );
	p_del->pairs = ( addr_pair )( pc - PAIRS_SIZE( p_del ) );
	p_write->data_ptr = pc;
	pc += CHANG_ELEM_HEAD + MAx( PAIRS_SIZE( p_write ), WRITES_MAX_SIZE( p_write ) );
	p_write->pairs = ( addr_pair )( pc - PAIRS_SIZE( p_write ) );
	p_write->prev_pairs = pc;
	pc += PAIRS_SIZE( p_write );
	p_new->data_ptr = pc;
	pc += CHANG_ELEM_HEAD + MAX( PAIRS_SIZE( p_new ), NEWS_MAX_SIZE( p_new ) );
	p_new->pairs = ( addr_pair )( pc - PAIRS_SIZE( p_new ) );
	data_ptr = pc;
	pc += pac.write_block_size + pac.new_block_size;
	prev_data_ptr = pc;
	p_data->data = prev_data_ptr;
	p_data_head->original_len = pac.write_block_size + pac.new_block_size;
	
	pac.write_id_start = p_version->order_id[ p_version->block_num - 1 ].id + 1;
	pac.new_id_start = pac.write_id_start + p_write->num;
	//第二次遍历avl
	if( avl_traver_first( tp->avl, pac2_func, &pac ) < 0 ){
		MOON_PRINT_MAN( ERROR, "second travel avl error!" );
		goto back1;
	}
	qsort_addr_pair( p_del->pairs, p_del->num );
	qsort_addr_pair( p_write->pairs, p_write->num );
	qsort_addr_pair( p_write->prev_pairs, p_write->num );
	for( i = 0; i < p_write->num; i++){
		p_write->pairs[ i ].addr->tmp_id = pac.write_id_start + i;
	}
	//先处理write块的指针转换
	if( convert_blocks_to_buf( data_ptr, p_write->pairs
			, p_write->num, &copy_block, p_version, tp->avl, 1 ) < 0 
		|| convert_blocks_to_buf( data_ptr + pac.write_block_size, p_new->pairs
			, p_new->num, &copy_block, p_version, tp->avl, 1 ) < 0 
		|| convert_blocks_to_buf( prev_data_ptr, p_write->prev_pairs
			, p_write->num, &copy_block, p_version, NULL, 0 ) < 0 ){
		MOON_PRINT_MAN( ERROR, "convert blocks to buffer error!" );
		goto back1;
	}
	pthread_mutex_unlock( &tp->avl_mutex );
	//计算所有block加起来的md5
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
	p_update_head->to_version = p_version->version + 1;
	p_update_head->from_version = p_version->version;
	//计算修改的block对应的原始版本，并与现在的异或
	block_xor( p_data->data, prev_data, pac.write_block_size );
	p_data_head->mask |= IS_XOR;
	//压缩整个数据快
	if( ( tmp1 = block_compress( data_ptr, p_data->data, p_data->original_len, p_data->original_len ) ) < 0 ){
		MOON_PRINT_MAN( ERROR, "compress error" );
		goto back2;
	}
	MOON_PRINT_MAN( TEST, "before compress len:%d , after compress len:%d .", p_data->original_len, tmp1 );
	p_data->len = tmp1;
	p_data_head->mask |= IS_COMPRESS;
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
	tmp1 =  sizeof( pac.update_head ) + p_del->len 
			+ p_write->len + p_new->len + sizeof( *p_data_head ) + p_data->len;
	if( ( pkt_buf = packet_buf_malloc( tmp1 + sizeof( *p_head ) ) ) == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc packet buf error!" );
		goto back2;
	}
	pc = pkt_buf;
	p_head = ( packet_head )pc;
	pack_packet_head( p_head, p_version->entity, MOON_UPDATE, tmp1 );
	pc += sizeof( *p_head );
	
	convert_uint32_t( &p_update_head->from_version );
	convert_uint32_t( &p_update_head->to_version );
	convert_uint16_t( &p_update_head->info_bits );
	memcpy( pc, p_update_head, sizeof( *p_update_head ) );
	pc += sizeof( *p_update_head );
	
	for( i = 0; i < 3; i++ ){
		if( pac.changes[ i ].len > 0 ){
			memcpy( pc, pac.changes[ i ].pairs, pac.changes[ i ].len );
			pc += pac.changes[ i ].len;
		}
	}
	convert_uint16_t( &p_data_head->mask );
	convert_uint32_t( &p_data_head->original_len );
	memcpy( pc, p_data_head, sizeof( *p_data_head ) );
	pc += sizeof( *p_data_head );
	memcpy( pc, p_data->data, p_data->len );

	*p_buf = pkt_buf;
	*p_len = tmp1 + sizeof( *p_head );
	ret = 0;
	goto back2;
back1:
	pthread_mutex_unlock( &tp->avl_mutex );
back2://释放分配的内存
	IF_FREE( buf );
	return ret;
}

static inline int packet_write_data_id( packet_change p_write )
{	
	char * pc;
	int i;
	uint64_t prev_64, tmp_64, new_64;
	mem_block p_block;

	pc = p_write->data_ptr;
	p_write->len = 0;
	if( convert_to_moon_num( ( void ** )&pc, p_write->num, &p_write->len ) < 0 ){
		MOON_PRINT_MAN( ERROR, "convert to moon num error!" );
		return -1;
	}
	prev_64 = 0;
	for( i = 0; i < p_write->num; i++ ){
		p_block = p_write->prev_pairs[ i ].addr;
		tmp_64 = p_block->id - prev_64;
		prev_64 = p_block->id;
		p_block = p_wrip->pairs[ i ].addr;
		new_64 = p_block->id - prev_64;
		if( convert_to_moon_num( ( void ** )&pc, tmp_64, &p_write->len ) < 0
			|| convert_to_moon_num( ( void ** )&pc, new_64, &p_write->len ) < 0 ){
			MOON_PRINT_MAN( ERROR, "convert to moon num error!" );
			return -1;
		}
	}
	return 0;
}

static inline int packet_new_data_id( packet_change p_new )
{	
	char * pc;
	int i;
	uint64_t prev_64, tmp_64;
	mem_block p_block;

	pc = p_new->data_ptr;
	p_new->len = 0;
	if( convert_to_moon_num( ( void ** )&pc, p_new->num,  &p_new->len ) < 0 ){
		MOON_PRINT_MAN( ERROR, "convert to moon num error!" );
		return -1;
	}
	prev_64 = 0;
	for( i = 0; i < p_new->num; i++ ){
		p_block = p_new->pairs[ i ].addr;
		tmp_64 = p_block->id - prev_64;
		prev_64 = p_block->id;
		if( convert_to_moon_num( ( void ** )&pc, p_block->type_id, &p_new->len ) < 0 
			|| convert_to_moon_num( ( void ** )&pc, p_block->type_num, &p_new->len ) < 0 
			|| convert_to_moon_num( ( void ** )&pc, tmp_64, &p_new->len ) < 0 ){
			MOON_PRINT_MAN( ERROR, "convert to moon num error!" );
			return -1;
		}
	}
	return 0;
}

static int get_update_packet( version_info p_from_version, version_info p_version, char ** p_buf, int * p_len )
{
	packet_s pac;
	packet_change p_del, p_write, p_new;
	packet_data p_data;
	packet_data_head p_data_head;
	packet_update_head p_update_head;
	packet_head p_head;
	int i, j, k, tmp1, ret;
	mem_block p_block;
	char *pc, *buf, *pkt_buf, *data_ptr, *prev_data_ptr;
	block_copy_s copy_block;
	addr_pair p_pair;

	ret = -1;
	memset( &pac, 0, sizeof( pac ) );
	pac.from_all = p_version->entity->shadow_type->struct_types;
	pac.to_all = p_version->entity->remote_all;
	pac.version = p_version;
	copy_block.flag = 0;
	copy_block.src_type = pac.from_all;
	copy_block.dst_type = pac.to_all;
	p_del = &pac.changes[ 0 ];
	p_write = &pac.changes[ 1 ];
	p_new = &pac.changes[ 2 ];
	p_data = &pac.data;
	p_data_head = &p_data.head
	p_update_head = &pac.update_head;	
	
	if( inc_version_user_num( p_from_version ) < 0 ){
		goto back1;
	}
	if( inc_version_user_num( p_version ) < 0 ){
		goto back2;
	}
	if( p_version->block_num <= 0 || p_from_version->block_num <= 0 ){
		MOON_PRINT_MAN( ERROR, "version error!" );
		goto back;
	}
	for( i = p_version->block_num - 1; i >= 0 
		&& p_version->order_id[ i ].id > p_from_version->order_id[ p_from_version->block_num - 1 ].id; i-- ){
		p_block = p_version->order_id[ i ].addr;
		p_pair = search_addr_id( p_from_version->order_addr
				, ( uintptr_t )p_block->virtual_addr, p_from_version->block_num );
		tmp1 = get_blockbuf_len( p_block, pac.to_all );
		if( tmp1 <= 0 ){
			MOON_PRINT_MAN( ERROR, "block len error!" );
			goto back;
		}
		if( p_pair != NULL && p_pair->addr->virtual_addr == p_block->virtual_addr ){//writed
			p_write->num++;	
			pac.write_block_size += tmp1;
			SET_BIT( bit_map, i );
		}else{//new
			p_new->num++;
			pac.new_block_size += tmp1;
		}
	}
	if( p_write->num == 0 ){
		MOON_PRINT_MAN( ERROR, "write num error!" );
		goto back;
	}
	p_del->num = p_from_version->block_num - ( i + 1 ) - p_write->num;
	//这是一段不太好的代码，试图把临时内存分配在一块内存上
	tmp1 = 0;
	tmp1 += CHANG_ELEM_HEAD + MAx( PAIRS_SIZE( p_del ), DELS_MAX_SIZE( p_del ) );
	tmp1 += CHANG_ELEM_HEAD + MAx( PAIRS_SIZE( p_write ), WRITES_MAX_SIZE( p_write ) ); 
	tmp1 += PAIRS_SIZE( p_write );
	tmp1 += CHANG_ELEM_HEAD + MAX( PAIRS_SIZE( p_new ), NEWS_MAX_SIZE( p_new ) );
	tmp1 += ( pac.write_block_size + pac.new_block_size ) * 2;
	buf = malloc( tmp1 );
	if( buf == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		goto back;
	}
	pc = buf;
	p_del->data_ptr = pc;
	pc += CHANG_ELEM_HEAD + MAX( PAIRS_SIZE( p_del ), DELS_MAX_SIZE( p_del ) );
	p_del->pairs = ( addr_pair )( pc - PAIRS_SIZE( p_del ) );
	p_write->data_ptr = pc;
	pc += CHANG_ELEM_HEAD + MAx( PAIRS_SIZE( p_write ), WRITES_MAX_SIZE( p_write ) );
	p_write->pairs = ( addr_pair )( pc - PAIRS_SIZE( p_write ) );
	p_write->prev_pairs = pc;
	pc += PAIRS_SIZE( p_write );
	p_new->data_ptr = pc;
	pc += CHANG_ELEM_HEAD + MAX( PAIRS_SIZE( p_new ), NEWS_MAX_SIZE( p_new ) );
	p_new->pairs = ( addr_pair )( pc - PAIRS_SIZE( p_new ) );
	data_ptr = pc;
	pc += pac.write_block_size + pac.new_block_size;
	prev_data_ptr = pc;
	p_data->data = prev_data_ptr;
	p_data_head->original_len = pac.write_block_size + pac.new_block_size;
	
	for( i = 0; j = 0, k = 0; i < p_from_version->block_num; i++ ){
		p_block = p_from_version->order_id[ i ].addr;
		p_pair = search_addr_id( p_version->order_addr
				, ( uintptr_t )p_block->virtual_addr, p_version->block_num );
		if( p_pair == NULL || p_pair->addr->virtual_addr != p_block->virtual_addr ){//del
			p_del->pairs[ j++ ] = p_from_version->order_id[ i ];
		}else if( p_pair->addr->id != p_block->id ){//write
			p_write->pairs[ k ] = *p_pair;
			p_write->prev_pairs[ k++ ] = p_from_version->order_id[ i ];
		}
	}
	if( j != p_del->num || k != p_write->num ){
		MOON_PRINT_MAN( ERROR, "del or write num error!" );
		goto error;
	}
	for( i = p_version->block_num - 1, j = p_new->num -1; i >= 0 && j >= 0 
		&& p_version->order_id[ i ].id > p_from_version->order_id[ p_from_version->block_num - 1 ].id; i-- ){
		p_block = p_version->order_id[ i ].addr;
		p_pair = search_addr_id( p_from_version->order_addr
				, ( uintptr_t )p_block->virtual_addr, p_from_version->block_num );
		if( p_pair == NULL || p_pair->addr->virtual_addr != p_block->virtual_addr ){//new
			p_new->pairs[ j-- ] = p_version->order_id[ i ];
		}
	}
	//先处理write块的指针转换
	if( convert_blocks_to_buf( data_ptr, p_write->pairs
			, p_write->num, &copy_block, p_version, NULL, 0 ) < 0 
		|| convert_blocks_to_buf( data_ptr + pac.write_block_size, p_new->pairs
			, p_new->num, &copy_block, p_version, NULL, 0 ) < 0 
		|| convert_blocks_to_buf( prev_data_ptr, p_write->prev_pairs
			, p_write->num, &copy_block, p_from_version, NULL, 0 ) < 0 ){
		MOON_PRINT_MAN( ERROR, "convert blocks to buffer error!" );
		goto error;
	}
	//copy md5
	memcpy( p_update_head->to_md5, p_version->md5_sum, sizeof( p_update_head->to_md5 ) );
	memcpy( p_update_head->from_md5, p_from_version->md5_sum, sizeof( p_update_head->from_md5 ) );
	p_update_head->to_version = p_version->version;
	p_update_head->from_version = p_from_version->version;
	//计算修改的block对应的原始版本，并与现在的异或
	block_xor( data_ptr, prev_data_ptr, pac.write_block_size );
	p_data_head->mask |= IS_XOR;
	//压缩整个数据快
	if( ( tmp1 = block_compress( data_ptr, p_data->data, p_data->original_len, p_data->original_len ) ) <= 0 ){
		MOON_PRINT_MAN( ERROR, "compress error" );
		goto error;
	}
	MOON_PRINT_MAN( TEST, "before compress len:%d , after compress len:%d .", p_data->original_len, tmp1 );
	p_data->len = tmp1;
	p_data_head->mask |= IS_COMPRESS;
	//打包del数据
	if( p_del->num > 0 ){
		p_update_head->info_bits |= HAS_DELS;
		if( packet_del_data( p_del ) < 0 ){
			MOON_PRINT_MAN( ERROR, "packet dels data error!" );
			goto error;
		}
	}
	//打包write数据
	p_update_head->info_bits |= ( HAS_WRITES | HAS_WRITES_ID );
	if( packet_write_data_id( p_write ) < 0 ){
		MOON_PRINT_MAN( ERROR, "packet writes data error!" );
		goto error;
	}
	//打包new数据
	if( p_new->num > 0 ){
		p_update_head->info_bits |= ( HAS_NEWS | HAS_WRITES_ID );
		if( packet_new_data_id( p_new ) < 0 ){
			MOON_PRINT_MAN( ERROR, "packet new data error!" );
			goto error;
		}
	}
	//组装所有部件
	tmp1 = sizeof( pac.update_head ) + p_del->len 
			+ p_write->len + p_new->len + sizeof( *p_data_head ) + p_data->len;
	if( ( pkt_buf = packet_buf_malloc( tmp1 + sizeof( *p_head ) ) ) == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc packet buf error!" );
		goto error;
	}
	pc = pkt_buf;
	p_head = ( packet_head )pc;
	pack_packet_head( p_head, p_version->entity, MOON_UPDATE, tmp1 );
	pc += sizeof( *p_head );
	
	convert_uint16_t( &p_update_head->info_bits );
	convert_uint32_t( &p_update_head->from_version );
	convert_uint32_t( &p_update_head->to_version );
	memcpy( pc, p_update_head, sizeof( *p_update_head ) );
	pc += sizeof( *p_update_head );
	
	for( i = 0; i < 3; i++ ){
		if( pac.changes[ i ].len > 0 ){
			memcpy( pc, pac.changes[ i ].pairs, pac.changes[ i ].len );
			pc += pac.changes[ i ].len;
		}
	}

	convert_uint16_t( &p_data_head->mask );
	convert_uint32_t( &p_data_head->original_len );
	memcpy( pc, p_data_head, sizeof( *p_data_head ) );
	pc += sizeof( *p_data_head );
	memcpy( pc, p_data->data, p_data->len );

	*p_buf = pkt_buf;
	*p_len = tmp1 + sizeof( *p_head );
	ret = 0;
error:
	free( buf );
back:
	dec_version_user_num( p_version );
back2:
	dec_version_user_num( p_from_version );
back1:
	return ret;
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
	p_change->len = sizeof( *p_change->p_ids ) * p_change->num;
	p_change->p_ids = malloc( p_change->len );
	if( p_change->p_ids == NULL ){
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
		p_change->p_ids[ i ].id_array[ 0 ] = u64_tmp;
	}
	return 0;
}

static inline int unpack_new_data( packet_change p_change, char ** pp_buf, int * p_len )
{
	uint64_t u64_tmp, u64_array[ 2 ];
	int i;

	if( get_from_moon_num( ( void ** )pp_buf, &u64_tmp, p_len ) < 0 ){
		MOON_PRINT_MAN( ERROR, "get news num error!" );
		return -1;
	}
	p_change->num = u64_tmp;
	p_change->len = sizeof( *p_change->p_ids ) * p_change->num;
	p_change->p_ids = malloc( p_change->len );
	if( p_change->p_ids == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		return -1;
	}
	for( i = 0; i < p_change->num; i++ ){
		if( get_from_moon_num( ( void ** )pp_buf, &u64_array[ 0 ], p_len ) < 0 
			|| get_from_moon_num( ( void ** )pp_buf, &u64_array[ 1 ], p_len ) < 0 ){
			MOON_PRINT_MAN( ERROR, "get new block info error!" );
			return -1;
		}
		p_change->p_ids[ i ].id_array[ 0 ] = u64_array[ 0 ];
		p_change->p_ids[ i ].id_array[ 1 ] = u64_array[ 1 ];
	}
	return 0;
}

static inline int unpack_new_data_id( packet_change p_change, char ** pp_buf, int * p_len )
{
	uint64_t u64_tmp, u64_array[ 3 ];
	int i;

	if( get_from_moon_num( ( void ** )pp_buf, &u64_tmp, p_len ) < 0 ){
		MOON_PRINT_MAN( ERROR, "get news num error!" );
		return -1;
	}
	p_change->num = u64_tmp;
	p_change->len = sizeof( *p_change->p_ids ) * p_change->num;
	p_change->p_ids = malloc( p_change->len );
	if( p_change->p_ids == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		return -1;
	}
	u64_tmp = 0;
	for( i = 0; i < p_change->num; i++ ){
		if( get_from_moon_num( ( void ** )pp_buf, &u64_array[ 0 ], p_len ) < 0 
			|| get_from_moon_num( ( void ** )pp_buf, &u64_array[ 1 ], p_len ) < 0
			|| get_from_moon_num( ( void ** )pp_buf, &u64_array[ 2 ], p_len ) < 0 ){
			MOON_PRINT_MAN( ERROR, "get new block info error!" );
			return -1;
		}
		u64_tmp += u64_array[ 2 ];
		p_change->p_ids[ i ].id_array[ 2 ] = u64_tmp;
		p_change->p_ids[ i ].id_array[ 0 ] = u64_array[ 0 ];
		p_change->p_ids[ i ].id_array[ 1 ] = u64_array[ 1 ];
	}
	return 0;
}

static inline int unpack_write_data_id( packet_change p_change, char ** pp_buf, int * p_len )
{
	uint64_t u64_tmp, u64_array[ 2 ];
	int i;

	if( get_from_moon_num( ( void ** )pp_buf, &u64_tmp, p_len ) < 0 ){
		MOON_PRINT_MAN( ERROR, "get news num error!" );
		return -1;
	}
	p_change->num = u64_tmp;
	p_change->len = sizeof( *p_change->p_ids ) * p_change->num;
	p_change->p_ids = malloc( p_change->len );
	if( p_change->p_ids == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		return -1;
	}
	u64_tmp = 0;
	for( i = 0; i < p_change->num; i++ ){
		if( get_from_moon_num( ( void ** )pp_buf, &u64_array[ 0 ], p_len ) < 0 
			|| get_from_moon_num( ( void ** )pp_buf, &u64_array[ 1 ], p_len ) < 0 ){
			MOON_PRINT_MAN( ERROR, "get new block info error!" );
			return -1;
		}
		u64_tmp += u64_array[ 0 ];
		p_change->p_ids[ i ].id_array[ 0 ] = u64_tmp;
		p_change->p_ids[ i ].id_array[ 1 ] = u64_array[ 1 ] + u64_tmp;
	}
	return 0;
}

//ret: moon_update_ack_e
static int _create_new_version( version_info p_version, version_info * pp_new_version, char * buf, int len )
{
	packet_s pac;
	packet_update_head p_update_head;
	packet_change p_del, p_write, p_new;
	packet_data p_data;
	packet_data_head p_data_head;
	int ret, i, j, k, n, m, write_index, new_index, tmp;
	MD5_CTX ctx;
	char * pac_buf, * pac_end, * prev_data;
	addr_pair p_pair;
	version_info p_new_version;
	mem_block p_block, p_new_block;
	block_copy_s copy;
	
	memset( &pac, 0, sizeof( pac ) );
	p_new_version = NULL;
	prev_data = NULL;
	ret = MOON_PACKET_ERROR;
	pac.version = p_version;
	pac.to_all = p_version->entity->shadow_type->struct_types;
	pac.from_all = p_version->entity->remote_all;
	pac_buf = buf;
	pac_end = pac_buf + len;

	if( inc_version_user_num( p_version ) < 0 ){
		ret = MOON_INTERNAL_ERROR;
		goto back1;
	}
	//解析update_head
	p_update_head = &pac.update_head;
	if( pac_end - pac_buf < sizeof( *p_update_head ) ){
		MOON_PRINT_MAN( ERROR, "too short to get update head!" );
		goto back2;
	}
	*p_update_head = *( ( packet_update_head )pac_buf );
	pac_buf = ( char * )( ( packet_update_head )pac_buf + 1 );
	unpack_update_packet_head( p_update_head );
	if( memcmp( p_update_head->from_md5, p_version->md5_sum
			, sizeof( p_update_head->from_md5 ) ) != 0 ){
		ret = MOON_VERSION_TOO_OLD;
		MOON_PRINT_MAN( ERROR, "dont't base on this version!" );
		goto back2;
	}
	if( ( p_update_head->info_bits & HAS_WRITES ) == 0 ){
		MOON_PRINT_MAN( ERROR, "no update blocks!"  );
		goto back2;
	}
	len = pac_end - pac_buf;
	//处理del
	p_del = &pac.changes[ 0 ];
	if( ( p_update_head->info_bits & HAS_DELS ) 
			&& unpack_del_data( p_del, &pac_buf, &len ) < 0 ){
		MOON_PRINT_MAN( ERROR, "unpack dels data error!" );
		goto back;
	}
	//处理write
	p_write = &pac.changes[ 1 ];
	pac.write_id_start = p_version->order_id[ p_version->block_num - 1 ].id + 1;
	if( ( p_update_head->info_bits & HAS_NEW_ID ) == HAS_NEW_ID ){
		tmp = unpack_write_data_id( p_write, &pac_buf, &len );
	}else{
		tmp = unpack_del_data( p_write, &pac_buf, &len );
	}
	if(	tmp < 0 
		|| p_write->num <= 0
		|| ( p_write->prev_pairs = ( addr_pair )calloc( sizeof( addr_pair_s ), p_write->num ) ) == NULL ){
		MOON_PRINT_MAN( ERROR, "unpack writes data error!" );
		goto back;
	}
	//处理new
	p_new = &pac.changes[ 2 ];
	pac.new_id_start = pac.write_id_start + p_write->num;
	if( ( p_update_head->info_bits & HAS_NEWS ) != 0 ){
		if( ( p_update_head->info_bits & HAS_NEW_ID ) == 0 ){
			tmp = unpack_new_data( p_new, &pac_buf, &len );
		}else{
			tmp = unpack_new_data_id( p_new, &pac_buf, &len );
		}
		if( tmp < 0 ){
			MOON_PRINT_MAN( ERROR, "unpack new data error!" );
			goto back;
		}
	}
	//创建新的version
	n = p_version->block_num + p_new->num - p_del->num;
	m = p_write->num + p_del->num;
	write_index = p_version->block_num - m;
	new_index = write_index + p_write->num;
	if( n < 0 || m > p_version->block_num ){
		MOON_PRINT_MAN( ERROR, "total or dels blocks num under zero!" );
		goto back;
	}
	p_new_version = version_surface_malloc( n );
	if( p_new_version == NULL ){
		MOON_PRINT_MAN( ERROR, "dump new version error!");
		goto back;
	}
	p_new_version->version = p_update_head->to_version;
	for( i = 0, j = 0, k = 0, n = 0, m = 0; i < p_version->block_num; i++ ){
		p_pair = &p_version->order_id[ i ];
		if( j < p_del->num ){
			if( p_del->p_ids[ j ].id_array[ 0 ] < p_pair->id ){
				MOON_PRINT_MAN( ERROR, "illegal del block id" );
				goto error;
			}else if( p_del->p_ids[ j ].id_array[ 0 ] == p_pair->id ){
				j++;
				m++;
				continue;
			}
		}
		if( k < p_write->num ){
			if( p_write->p_ids[ k ].id_array[ 0 ] < p_pair->id ){
				MOON_PRINT_MAN( ERROR, "illegal write block id" );
				goto error;
			}else if( p_write->p_ids[ k ].id_array[ 0 ] == p_pair->id ){
				p_write->prev_pairs[ k ] = *p_pair;
				if( ( p_new_block = mem_block_surface_dump( p_pair->addr ) ) == NULL ){
					MOON_PRINT_MAN( ERROR, "dump write block error!" );
					goto error;
				}
				if( ( p_update_head->info_bits & HAS_NEW_ID ) == 0 ){
					p_new_block->id = pac.write_id_start + k;
				}else{
					p_new_block->id = p_write->p_ids[ k ].id_array[ 1 ];
				}
				p_new_version->order_id[ write_index + k ].id = p_new_block->id;
				p_new_version->order_id[ write_index + k ].addr = p_new_block;
				pac.write_block_size += get_blockbuf_len( p_pair->addr, pac.from_all );
				k++;
				m++;
				continue;
			}
		}
		mem_block_ref_inc( p_pair->addr );
		p_new_version->order_id[ n++ ] = *p_pair;
	}
	if( j < p_del->num || k < p_write->num ){
		MOON_PRINT_MAN( ERROR, "have illegal blocks" );
		goto error;
	}
	for( i = 0; i < p_new->num; i++ ){
		if( ( p_new_block = create_new_block( pac.to_all
				, p_new->p_ids->id_array[ 0 ], p_new->p_ids->id_array[ 1 ] ) ) == NULL ){
			MOON_PRINT_MAN( ERROR, "create new block error!" );
			goto error;
		}
		p_new_block->version = p_new_version->version;
		if( ( p_update_head->info_bits & HAS_NEW_ID ) == 0 ){
			p_new_block->id = pac.new_id_start + i;
		}else{
			p_new_block->id = p_new->p_ids->id_array[ 2 ];
		}
		p_new_version->order_id[ new_index + i ].id = p_new_block->id;
		p_new_version->order_id[ new_index + i ].addr = p_new_block;
		pac.new_block_size += get_blockbuf_len( p_new_block, pac.from_all );
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
	pac_buf = ( char * )( ( packet_data_head )pac_buf + 1 );
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
	if( convert_blocks_to_buf( prev_data, p_write->prev_pairs
			, p_write->num, &copy, p_version, NULL, 0 ) < 0 ){
		MOON_PRINT_MAN( ERROR, "convert block to buf error!" );
		goto error;
	}
	block_xor( p_data->data, prev_data, pac.write_block_size );
	copy.flag = 1;
	copy.src_type = pac.from_all;
	copy.dst_type = pac.to_all;
	if( convert_blocks_from_buf( p_new_version->order_id + write_index, p_data->data
			, p_write->num + p_new->num, &copy, p_new_version ) < 0 ){
		MOON_PRINT_MAN( ERROR, "get blocks from buf error!" );
		goto error;
	}
	if( ( p_update_head->info_bits & HAS_NEW_ID ) != 0 ){
		qsort_addr_pair( p_new_version->order_id, p_new_version->block_num );
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
	entity_ref_inc( p_version->entity );
	p_new_version->entity = p_version->entity;
	*pp_new_version = p_new_version;
	ret = MOON_UPDATE_SUCCESS;
	goto back;
error:
	p_new_version->status = STATUS_CLOSED;
	for( i = 0; i < p_new_version->block_num; i++ ){
		p_block = p_new_version->order_id[ i ].addr;
		if( p_block != NULL ){
			mem_block_ref_dec( p_block );
		}
	}
	version_ref_dec( p_new_version );
back:
	IF_FREE( prev_data );
	IF_FREE( p_del->p_ids );
	IF_FREE( p_write->p_ids );
	IF_FREE( p_write->prev_pairs );
	IF_FREE( p_new->p_ids );
	IF_FREE( p_data->data );
back2:
	dec_version_user_num( p_version );
back1:
	return ret;
}

static inline int pack_packet_head( packet_head p_head, shadow_entity p_entity, int cmd, int len )
{
	p_head->cmd = cmd;
	p_head->len = len;
	memcpy( p_head->version, PACKET_VERSION, sizeof( p_head->version ) );
	get_key_of_value( p_entity->shadow_type, p_head->protocol, sizeof( p_head->protocol ) );
	memcpy( p_head->md5_sum, p_entity->md5_sum, sizeof( p_head->md5_sum ) );
	convert_uint16_t( &p_head->cmd );
	convert_uint32_t( &p_head->len );
	return 0;
}

static inline int unpack_packet_head( packet_head p_head )
{
	if( memcmp( p_head->version, PACKET_VERSION, sizeof( p_head->version ) ) != 0 ){
		return -1;
	}
	convert_uint16_t( &p_head->cmd );
	convert_uint32_t( &p_head->len );
	return 0;
}

static inline int unpack_update_packet_head( packet_update_head p_head )
{
	convert_uint32_t( &p_head->from_version );
	convert_uint32_t( &p_head->to_version );
	convert_uint16_t( &p_head->info_bits );
	if( p_head->to_version <= p_head->from_version ){
		return -1;
	}
	return 0;
}

int shadow_commit( )
{

}

static inline void print_version( version_info p_version )
{
#define PRINT_PAIR( p_pair ) ({\
	printf( "virtual_addr:0x%llx->", p_pair->virtual_addr );\
	0;\
})
	ARRAY_TRAVER( p_version->order_addr, p_version->block_num, PRINT_PAIR );
#undef PRINT_PAIR
}

#ifdef LEVEL_TEST
void shadow_print_types( char * type )
{
	shadow_type p_type;

	p_type = ( shadow_type )hash_search( shadow_head, type, 0 );
	if( p_type != NULL ){
		print_struct_all( p_type->struct_types );
	}
}

void shadow_print_runtime( )
{
	thread_private tp;

	tp = shadow_env_get();
	if( tp == NULL ){
		return;
	}
	print_version( tp->version );
	printf( "\n" );
	avl_print( tp->avl );
	printf( "\n" );
}
#endif

typedef struct stream_interface_s{
	struct stream_interface_s * ( * new_stream )( common_user_data_u user_data );
	int  ( * send_packet )( common_user_data_u user_data, buf_head p_buf_head, int is_pushed );
	int  ( * recv_next_packet )( common_user_data_u user_data, int timeout_ms );
	int  ( * stream_set_callbacks )( common_user_data_u common_user_data, stream_callbacks p_callbacks );
	void ( * stream_close )( common_user_data_u user_data );
	void ( * inc_ref )( common_user_data_u user_data );
	void ( * dec_ref )( common_user_data_u user_data );
	common_user_data_u user_data;
} stream_interface_s;
typedef stream_interface_s * stream_interface;

static void point_ref_inc( linked_point p_point )
{
	int tmp;

	tmp = __sync_add_and_fetch( &p_point->ref_num, 1 );
	if( tmp < 0 ){
		MOON_PRINT_MAN( ERROR, "point ref num up overflow!" );
	}
}

static void point_ref_dec( linked_point p_point )
{
	unsigned tmp;

	tmp = __sync_sub_and_fetch( &p_point->ref_num, 1 );
	if( tmp == 0 ){
		if( !STATUS_LEGAL_CLOSED( p_point->status ) ){
			MOON_PRINT_MAN( ERROR, "point status inlegal when free!" );
		}
		dlist_free( p_point );
	}else if( tmp < 0 ){
		MOON_PRINT_MAN( ERROR, "point ref num down overflow" );
	}
}

static void point_close_func( linked_point p_point )
{
	if( p_point->p_entity != NULL ){
		notice_point_closed( p_point );
	}
	p_point->stream_i.stream_close( p_point->stream_i.user_data );
}

static void _point_close_func( linked_point p_point )
{
	CALL_FUNC( p_point->user_i.close_func, p_point );
	if( p_point->p_entity != NULL ){
		entity_ref_dec( p_point->p_entity );
	}
	CALL_FUNC( p_point->stream_i.dec_ref, p_point->stream_i.user_data );
}

static void close_point( linked_point p_point )
{
	int tmp;

	for( tmp = p_point->status
			; ( tmp & STATUS_MASK ) != STATUS_CLOSED; tmp = p_point->status ){
		if( tmp < USEING_REF_UNIT ){
			if( __sync_bool_compare_and_swap( &p_point->status, tmp, STATUS_CLOSED ) ){
				point_close_func( p_point );
				_point_close_func( p_point );
				break;
			}
		}else{
			if( __sync_bool_compare_and_swap( &p_point->status, tmp, ( tmp & ~STATUS_MASK ) | STATUS_CLOSED ) ){
				point_close_func( p_point );
				break;
			}
		}
	}
}

static int point_useing_ref_inc( linked_point p_point )
{
	return useing_ref_inc( &p_point->status );
}

static int point_useing_ref_dec( linked_point p_point )
{
	return useing_ref_dec( &p_point->status, _point_close_func, p_point );
}

static int is_point_in_entity( linked_point p_point )
{
	return ( dlist_prev( p_point ) != NULL || dlist_next( p_point ) != NULL );
}

static buf_head create_newest_ack_packet( shadow_entity p_entity )
{
	buf_head p_buf_head;
	buf_desc_s buf_desc;
	char * buf;
	int len;

	len = sizeof( packet_head_s );
	if( ( buf = packet_buf_malloc( len ) ) == NULL ){
		return NULL;
	}
	buf_desc.offset = 0;
	buf_desc.len = len;
	buf_desc.buf = buf;
	if( add_buf_to_packet( &p_buf_head, &buf_desc ) < 0 ){
		packet_buf_free( buf );
		return NULL;
	}
	pack_packet_head( ( packet_head )buf, p_entity, MOON_ALREADY_NEWEST, 0 );
	return p_buf_head;
}

static buf_head create_update_ack_packet( shadow_entity p_entity, int error_code )
{
	buf_head p_buf_head;
	buf_desc_s buf_desc;
	packet_update_ack p_ack_pac;
	char * buf;
	int len;

	len = sizeof( packet_head_s + packet_update_ack_s );
	if( ( buf = packet_buf_malloc( len ) ) == NULL ){
		return NULL;
	}
	buf_desc.offset = 0;
	buf_desc.len = len;
	buf_desc.buf = buf;
	if( add_buf_to_packet( &p_buf_head, &buf_desc ) < 0 ){
		packet_buf_free( buf );
		return NULL;
	}
	p_ack_pac = ( packet_update_ack )( buf + sizeof( packet_head_s ) );
	pack_packet_head( ( packet_head )buf
			, p_entity, MOON_UPDATE_ACK, sizeof( packet_update_ack_s ) );
	p_ack_pac->status_code = error_code;
	convert_uint32_t( &p_ack_pac->status_code );
	return p_buf_head;
}

//服务器接收函数
static int server_recv_packet( linked_point p_point, buf_head p_buf_head )
{
	int len, ret, error_code, is_newest, is_unasso;
	char * buf, * query_ack_buf;
	packet_head_s head;
	packet_update_head_s update_head;
	packet_query_s query_packet;
	buf_head p_up_ack, p_query_ack;
	shadow_entity p_entity;
	shadow_type p_type;
	version_info p_base_version, p_new_version;
	buf_desc_s buf_desc;
	char md5_buf[ 2 * sizeof( p_base_version->md5_sum ) + 1 ];

	is_newest = is_unasso = 0;
	ret = start_notice = -1;
	p_new_version = p_base_version = NULL;
	p_entity = p_point->p_entity;
	p_query_ack = NULL;
	begin_travel_packet( p_buf_head );
	if( get_next_buf( p_buf_head, &buf, &len ) < sizeof( head ) ){
		MOON_PRINT_MAN( ERROR, "get buf error!" );
		goto packet_error;
	}
	head = *( packet_head )buf;
	if( p_entity == NULL ){//未认证的
		p_type = hash_search( shadow_head, head.protocol, 0 );
		if( p_type == NULL ){
			MOON_PRINT_MAN( ERROR, "no such protocol!" );
			goto packet_error;
		}
		sprint_binary( md5_buf, head.md5_sum, sizeof( head.md5_sum ) );
		pthread_mutex_lock( &p_type->entity_mutex );
		p_entity = hash_search( p_type->entity_table, md5_buf, 0 );
		if( p_entity != NULL && p_entity->is_shadow == 0 ){
			entity_ref_inc( p_entity );
		}else{
			p_entity = NULL;
		}
		pthread_mutex_unlock( &p_type->entity_mutex );
		if( p_entity == NULL ){
			MOON_PRINT_MAN( ERROR, "no such entity server!" );
			goto packet_error;
		}
		p_point->p_entity = p_entity;
		is_unasso = 1;
	}
	if( unpack_packet_head( &head ) < 0 
		|| memcmp( head.md5_sum, p_entity->md5_sum, sizeof( head.md5_sum ) ) != 0 
		|| len < head.len + sizeof( head ) ){
		MOON_PRINT_MAN( ERROR, "unpacket packet head error!" );
		goto packet_error;
	}
	switch( head.cmd ){
	case MOON_INIT:
		point_ref_inc( p_point );
		if( is_unasso
			&& add_point_to_entity( p_entity, p_point ) < 0 ){
				MOON_PRINT_MAN( ERROR, "add point to entity error!" );
				point_ref_dec( p_point );
				goto packet_error;
		}
		if( p_point->send_packet( p_point->stream_i.user_data, p_entity->init_ack_pac, 0 ) < 0 ){
			MOON_PRINT_MAN( ERROR, "send packet error!" );
			goto packet_error;
		}
		break;
	case MOON_UPDATE:
		if( head.len < sizeof( update_packet ) ){
			goto packet_error;
		}
		update_head = *( packet_update_head )( buf + sizeof( head ) );
		if( unpack_update_packet_head( &update_head ) < 0 ){
			MOON_PRINT_MAN( ERROR, "unpack update hhead error!" );
			goto packet_error;
		}	
		pthread_mutex_lock( &p_entity->version_mutex );
		if( !IS_ENTITY_CLOSED( p_entity ) ){
			p_base_version = p_entity->version_head;
			version_ref_inc( p_base_version );
		}
		pthread_mutex_unlock( &p_entity->version_mutex );
		if( p_base_version == NULL ){
			goto packet_error;
		}
		if( memcmp( p_base_version->md5_sum
				, update_head.md5_sum, sizeof( p_base_version->md5_sum ) ) != 0 ){
			error_code = MOON_VERSION_TOO_OLD;
		}else if( p_base_version->version != update_head.from_version 
			|| update_head.to_version != update_head.from_version + 1 
			|| ( update_head.info_bits & HAS_NEW_ID ) != 0 ){
			error_code = MOON_UPDATE_HEAD_ERROR;
		}else{
			error_code = _create_new_version( p_base_version
				, &p_new_version, buf + sizeof( head ), head.len );
		}
		if( error_code == MOON_UPDATE_SUCCESS ){
			p_new_version->p_buf_head = p_buf_head;
			p_buf_head = NULL:
			error_code = server_add_version_to_entity( p_entity, p_base_version, p_new_version );
		}
		version_ref_dec( p_base_version );
		if( error_code != MOON_UPDATE_SUCCESS && p_new_version != NULL ){
			set_version_status_old( p_new_version );
			version_ref_dec( p_new_version );
		}
		p_up_ack = create_update_ack_packet( p_entity, error_code );
		if( p_buf_head == NULL ){
			MOON_PRINT_MAN( ERROR, "create update ack packet error!" );
			goto packet_error;
		}
		if( p_point->send_packet( p_point->stream_i.user_data, p_up_ack, 0 ) < 0 ){
			free_total_packet( p_up_ack );
			MOON_PRINT_MAN( ERROR, "send update ack packet error!" );
			goto packet_error;
		}
		break;
	case MOON_QUERY:
		if( head.len < sizeof( packet_query_s ) ){
			goto packet_error;
		}
		query_packet = *( packet_query )( buf + sizeof( head ) );
		sprint_binary( md5_buf, query_packet.version_md5, sizeof( query_packet.version_md5 ) );
		pthread_mutex_lock( &p_entity->version_mutex );
		if( !IS_ENTITY_CLOSED( p_entity ) ){
			p_base_version = list_to_data ( hash_search( p_entity->version_table, md5_buf, 0 ) );
			if( p_base_version != NULL )
				if( p_base_version->version >= p_entity->cur_notice_version->version ){
					is_newest = 1;
				}else if( p_base_version->version + RESET_MIN_LATER_VERSION_NUM 
						> p_entity->cur_notice_version->version ){
					p_query_ack = dump_packet( p_base_version->p_buf_head );
				}else{
					p_new_version = p_entity->cur_notice_version;
					version_ref_inc( p_new_version );
					version_ref_inc( p_base_version );
				}
			}else{
				p_base_version = p_entity->root_version;
				version_ref_inc( p_base_version );
				p_new_version = p_entity->cur_notice_version;
				version_ref_inc( p_new_version );
			}
		}
		pthread_mutex_unlock( &p_entity->version_mutex );
		if( is_newest ){
			p_query_ack = create_newest_ack_packet( p_entity );
		}else if( p_new_version != NULL ){
			if( get_update_packet( p_base_version, p_new_version, &query_ack_buf, &len ) >= 0 ){
				buf_desc.offset = 0;
				buf_desc.len = len;
				buf_desc.buf = query_ack_buf;
				if( add_buf_to_packet( &p_query_ack, &buf_desc ) < 0 ){
					MOON_PRINT_MAN( ERROR, "add buf error!" );
					packet_buf_free( query_ack_buf );
				}
			}else{
				MOON_PRINT_MAN( ERROR, "create query ack buf error!" );
			}
			version_ref_dec( p_new_version );
			version_ref_dec( p_base_version );
		}
		if( p_query_ack == NULL ){
			MOON_PRINT_MAN( ERROR, "create query ack packet error!" );
			goto packet_error;
		}
		if( p_point->send_packet( p_point->stream_i.user_data, p_query_ack, 0 ) < 0 ){
			free_total_packet( p_query_ack );
			MOON_PRINT_MAN( ERROR, "send query ack packet error!" );
			goto packet_error;
		}
		break;
	default:
		MOON_PRINT_MAN( ERROR, "unkonwn packet type!" );
		goto packet_error;
	}
	ret = 0;

packet_error:
	if( p_buf_head != NULL ){
		free_total_packet( p_buf_head );
	}
	return ret;
}

//影子端的
static int on_pushed_data_arrive( buf_head p_buf_head, common_user_data_u user_data )
{
	linked_point p_point;
	shadow_entity p_entity;
	int ret_code;

	ret_code = -1;
	p_point = ( linked_point )user_data.ptr;
	if( point_useing_ref_inc( p_point ) < 0 ){
		MOON_PRINT_MAN( ERROR, "useing point error!" );
		free_total_packet( p_buf_head );
		return -1;
	}
	p_entity = p_point->p_entity;
	if( p_entity == NULL || is_entity_shadow( p_entity ) == 0 
		|| is_point_in_entity( p_point ) == 0 ){
		MOON_PRINT_MAN( ERROR, "entity is null or not shadow!" );
		free_total_packet( p_buf_head );
		goto back;
	}
	ret_code = entity_coming_update( p_entity, p_buf_head ); 
	if( ret_code < 0 ){
		MOON_PRINT_MAN( ERROR, "update packet error!" );
		goto back;
	}
	if( ret_code == UPDATEING ){
		ret_code = update_loop( p_entity );
	}
	if( ret_code == QUERYING ){
		if( ( ret_code = send_query( p_point ) ) < 0 ){
			close_point( p_point );
			MOON_PRINT_MAN( ERROR, "send query error!" );
			goto back;
		}
	}

back:
	point_useing_ref_dec( p_point );
	return ret_code;
}

static int on_data_arrive( buf_head p_buf_head, common_user_data_u user_data )
{
	linked_point p_point;
	int ret = -1;

	p_point = ( linked_point )user_data.ptr;
	if( point_useing_ref_inc( p_point ) < 0 ){
		MOON_PRINT_MAN( ERROR, "point useing ref add error!" );
		free_total_packet( p_buf_head );
		close_point( p_point );
		return -1;
	}
	if( p_point->user_i.can_recv_func( p_point, p_buf_head ) < 0 ){
		MOON_PRINT_MAN( ERROR, "recv data error!" );
		point_useing_ref_dec( p_point );
		close_point( p_point );
		return -1;
	}
	point_useing_ref_dec( p_point );
	return 0;
}

static void on_point_unlinking( common_user_data_u user_data )
{
	linked_point p_point;

	p_point = ( linked_point )user_data.ptr;
	close_point( p_point );
}

static void on_point_ref_dec( common_user_data_u user_data )
{
	linked_point p_point;

	p_point = ( linked_point )user_data.ptr;
	point_ref_dec( p_point );
}

static void on_point_ref_inc( common_user_data_u user_data )
{
	linked_point p_point;

	p_point = ( linked_point )user_data.ptr;
	point_ref_inc( p_point );
}

static int on_stream_linking( stream_interface p_stream_i )
{
	linked_point p_point;
	stream_callbacks_s callback;

	if( p_stream_i == NULL ){
		return -1;
	}
	p_point = ( linked_point )dlist_malloc( sizeof( *p_point ) );
	if( p_point == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc point error!" );
		return -1;
	}
	CALL_FUNC( p_stream_i->inc_ref, p_stream_i->user_data );
	p_point->stream_i = *p_stream_i;
	p_point->ref_num = 1;
	p_point->user_i.can_recv_func = server_recv_packet;
	callback.pushed_data_arrive = on_pushed_data_arrive;
	callback.data_arrive = on_data_arrive;
	callback.connect_close = on_point_unlinking;
	callback.user_data_ref_dec = on_point_ref_dec;
	callback.user_data_ref_inc = on_point_ref_inc;
	callback.user_data.ptr = p_point;

	point_useing_ref_inc( p_point );//这里肯定会成功
	if( p_stream_i->stream_set_callbacks( p_stream_i->user_data, &callback ) < 0 ){
		MOON_PRINT_MAN( ERROR, "set stream callbacks error!" );
		close_point( p_point );
		goto back;
	}
	if( p_stream_i->recv_next_packet( p_stream_i->user_data, 10000 ) < 0 ){
		MOON_PRINT_MAN( ERROR, "set stream can recv error!" );
		close_point( p_point );
		goto back;
	}
back:
	point_useing_ref_dec( p_point );
	point_ref_dec( p_point );
	return ret;
}

