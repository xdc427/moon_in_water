#include<stdio.h>
#include"moon_runtime.c"

/*
void avl_print( avl_tree avl );
void avl_del( avl_tree * pavl );
addr_pair avl_search( avl_tree avl, void * addr );
addr_pair avl_add( avl_tree * pavl, void * addr );
addr_pair_s avl_del( avl_tree * pavl, void * addr );
*/

static avl_tree avl_head = NULL;

void main()
{
	char cmd;
	int num, i, data;
	addr_pair ret;

	printf("r:restart\na:add\nd:delete\ns:search\nq:quit\n");
	while( 1 ){
		scanf( "%c", &cmd );
		switch( cmd ){
		case 'r':
			avl_free( &avl_head );
			break;
		case 'a':
			num = 0;
			scanf( "%d", &num );
			for( i = 0; i < num; i++ ){
				data = 0;
				scanf( "%d", &data );
				avl_add( &avl_head, data );
				avl_print( avl_head );
			}
			break;
		case 'd':
			data = 0;
			scanf( "%d", &data );
			avl_del( &avl_head, data );
			avl_print( avl_head );
			break;
		case 's':
			data = 0;
			scanf( "%d", &data );
			ret = avl_search( avl_head, data );

			if( ret != NULL ){
				printf( "search ok:%d\n", ret->virtual_addr );
			}else{
				printf( "cant find\n" );
			}
			break;
		case 'q':
			avl_free( &avl_head );
			return;
		case ' ':
		case '\t':
		case '\n':
			break;
		default:
			printf( "cmd error:%c!\n", cmd );
		}
	}
}
