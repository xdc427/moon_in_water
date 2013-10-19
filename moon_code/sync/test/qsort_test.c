#include<stdio.h>
#include"moon_runtime.c"

void print( addr_pair pair, int num )
{
	int i;

	for( i = 0; i < num; i++ ){
		printf( "%d:", pair[ i ].virtual_addr );
	}
	printf( "\n" );
}
void main()
{
	char cmd;
	int num, i, data;
	addr_pair pair ,ret;

	num = 0;
	pair = NULL;
	while( 1 ){
		scanf( "%c", &cmd );
		switch( cmd ){
		case 'q':
			return;
		case 'a':
			num = 0;
			scanf( "%d", &num );
			if( num < 0 ){
				break;
			}
			pair = malloc( sizeof( addr_pair_s ) * num );
			for( i = 0; i < num; i++ ){
				scanf( "%d", &data );
				pair[ i ].virtual_addr = data;
			}
			qsort_addr_pair( pair, num );
			print( pair, num );
			break;
		case 's':
			scanf( "%d", &data );
			ret = search_addr_id( pair, data, num );
			if( ret != NULL ){
				printf( "search ok:%d\n", ret->virtual_addr );
			}else{
				printf( "search fail\n");
			}
			break;
		case 'r':
			if( pair != NULL ){
				free( pair );
				pair = NULL;
				num = 0;
			}
			break;
		case ' ':
		case '\t':
		case '\n':
			break;
		default:
			printf( "cmd error!\n" );
		}
	}
}
