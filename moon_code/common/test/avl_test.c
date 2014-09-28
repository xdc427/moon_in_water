#include"moon_common.h"
#include<stdio.h>

void avl_test()
{
	avl_tree avl_head = NULL;
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
//				avl_print( avl_head );
			}
			avl_print( avl_head );
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
				printf( "search ok:%lld\n", ret->virtual_addr );
			}else{
				printf( "cant find\n" );
			}
			break;
		case 'q':
			avl_free( &avl_head );
			return;
		case 'p':
			avl_print( avl_head );
			break;
		case ' ':
		case '\t':
		case '\n':
			break;
		default:
			printf( "cmd error:%c!\n", cmd );
		}
	}
}

int main()
{
	avl_test();
	return 0;
}

