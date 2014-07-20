#include<stdio.h>
#include"moonpp.h"
#include"moon_debug.h"

#define SERVER "xdc"
#define DATA_LEN 10

SHADOW_STRUCT struct list_s{
	int id;
	struct list_s * next;
};
typedef struct list_s list_s;
typedef struct list_s * list;


SHADOW_VAR struct list_s * head = NULL ; //shadow变量目前不能用别名声明
SHADOW_VAR int data[ 10 ];
SHADOW_VAR int * data_point[ 10 ];

void offset_test()
{
	int i;

	for( i = 0; i < DATA_LEN; i++ ){
		SHADOW_SET( &data[ i ], i );
		SHADOW_SET( &data_point[ i ], &data[ i ] );
	}
}

void offset_print()
{
	int i;
	SHADOW_POINT int * tmp;

	for( i = 0; i < DATA_LEN; i++ ){
		tmp = SHADOW_GET( &data_point[ i ] );
		printf( "| %d:%d ", i, SHADOW_GET( tmp ) );
	}
	printf( "\n" );
}

void add( int id )
{
	SHADOW_POINT struct list_s ** pp_tmp;
	SHADOW_POINT struct list_s * new, * p_tmp;
	pp_tmp = SHADOW_ADDRESS( &head );
	for( ; SHADOW_POINT_CMP( pp_tmp, !=, NULL )
			; p_tmp = SHADOW_GET( pp_tmp ), pp_tmp = SHADOW_ADDRESS( &p_tmp->next ) ){
		MOON_PRINT_MAN( TEST, "%p\n", pp_tmp );
	}
	new = SHADOW_NEW( struct list_s, 1 );
	SHADOW_PRINT_RUNTIME();
	MOON_PRINT_MAN( TEST, "%p\n", new );
	SHADOW_SET( &new->id, id );
	SHADOW_SET( pp_tmp, new );
}

void del( int id )
{
	SHADOW_POINT struct list_s ** pp_tmp;
	SHADOW_POINT struct list_s * p_tmp, * p_next;
	int cur_id;

	pp_tmp = SHADOW_ADDRESS( &head );
	for( ; SHADOW_POINT_CMP( pp_tmp, !=, NULL ); pp_tmp = SHADOW_ADDRESS( &p_tmp->next ) ){
		MOON_PRINT_MAN( TEST, "%p\n", pp_tmp );
		p_tmp = SHADOW_GET( pp_tmp );
		cur_id = SHADOW_GET( &p_tmp->id );
		if( id == cur_id ){
			p_next = SHADOW_GET( &p_tmp->next );
			SHADOW_SET( pp_tmp, p_next );
			SHADOW_DEL( p_tmp );
			break;
		}
	}
}

void print_list()
{
	SHADOW_POINT struct list_s * tmp;

	for( tmp = SHADOW_GET( &head ); SHADOW_POINT_CMP( &tmp, !=, NULL ) 
			; tmp = SHADOW_GET( &tmp->next ) ){
		printf( "%d->", SHADOW_GET( &tmp->id ) );
	}
	printf( "\n" );
}

void print_native_list()
{
	struct list_s * tmp;

	for( tmp = head; tmp != NULL; tmp = tmp->next ){
		printf( "%d->", tmp->id );
	}
	printf( "\n" );
}

int test_sync()
{
	void * env, *env2;

	SHADOW_INIT();
	SHADOW_PRINT_TYPES();
	if( SHADOW_SERVER( SERVER ) < 0 ){
		MOON_PRINT_MAN( ERROR, "init shadow error!" );
		return -1;
	}
	env = SHADOW_ENV( SERVER, NEWEST_VERSION );
	if( env == NULL ){
		MOON_PRINT_MAN( ERROR, "create shadow env error!" );
		return -1;
	}
	SHADOW_ENV_SET( env );
	SHADOW_PRINT_RUNTIME();
	add( 1 );

	offset_test();

	SHADOW_PRINT_RUNTIME();
	print_list();
	offset_print();
	if( SHADOW_SYNC() < 0 ){
		MOON_PRINT_MAN( ERROR, "shadow commit error!" );
		return -1;
	}
	env2 = SHADOW_ENV( SERVER, NEWEST_VERSION );
	if( env2 == NULL ){
		MOON_PRINT_MAN( ERROR, "create writed env error!" );
		return -1;
	}
	SHADOW_ENV_SET( env2 );
	print_list();
	offset_print();
	print_native_list();
	return 0;
}

void main()
{
	//测试avl
//	avl_test();
	//测试moon num
	if( test_moon_num() < 0 ){
		printf( "test moon num error!\n" );
	}else{
		printf( "test moon num success!\n" );
	}

	//测试sync模块整体
	if( test_sync() < 0 ){
		printf( "test sync error!\n" );
	}else{
		printf( "test sync success!\n" );
	}		
}

