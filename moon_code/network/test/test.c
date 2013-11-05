#include<stdio.h>
#include"moon_packet.h"

void ins_model_cmd( packet_model p_model )
{
	char cmd;
	char elem_id[ 156 ];
	int len, position;
	char * p_buf;

	while( 1 ){
		scanf( "%c", &cmd );
		switch( cmd ){
		case 'c':
			if( create_packet_buf( p_model ) < 0 ){
				printf( "create buf error!\n" );
			}else{
				printf( "create buf success!\n" );
			}
			break;
		case 'p':
			packet_instantiation_model_print( p_model );
			break;
		case '1':
			scanf( "%255s %d",  elem_id, &len );
			if( set_packet_elem_len( p_model, elem_id, len ) < 0 ){
				printf( "set elem len error!\n" );
			}else{
				printf( "set elem len success!\n");
			}
			break;
		case '2':
			scanf( "%d %d", &position, &len );
			if( set_packet_elem_len_position( p_model, len, position ) < 0 ){
				printf( "set elem len position error!\n");
			}else{
				printf( "set elem len position success!\n" );
			}
			break;
		case 'l':
			len = get_packet_elem_len( p_model );
			printf( "cur elem len is:%d\n", len );
			break;
		case 'b':
			p_buf = get_packet_elem_buf( p_model );	
			printf( "cur buf point is:%p\n", p_buf );
			break;
		case 'n':
			if( next_packet_elem( p_model ) < 0 ){
				printf( "can't goto next elem!\n" );
			}else{
				printf( "goto next elem!\n" );
			}
			break;
		case 'q':
			free_packet( p_model );
			return;
		case ' ':
		case '\t':
		case '\n':
			break;
		default:
			printf( "cmd error:%d\n", cmd );
			break;
		}
	}
}

void main()
{
	char cmd;
	char model_id[ 256 ];
	packet_elem_s elem = { 0 };
	packet_model p_model;
	int len;

	while( 1 ){
		scanf( "%c", &cmd );
		switch( cmd ){
		case 'a':
			scanf( "%255s %255s %d", model_id, elem.id, &elem.len );
			if( packet_model_add( model_id, &elem, 0 ) < 0 ){
				printf( "add error!\n");
			}else{
				printf( "add success!\n" );
			}
			break;
		case 'v':
			packet_model_print();
			break;
		case 'p':
			scanf( "%255s", model_id );
			if( ( p_model = get_pack_instantiation( model_id ) ) == NULL ){
				printf( "get pack error!\n" );
			}else{
				ins_model_cmd( p_model );
			}
			break;
		case 'u':
			scanf( "%255s %d", model_id, &len );
			if( ( p_model = get_unpack_instantiation( model_id, malloc( 1 ), len ) ) == NULL ){
				printf( "get unpack error!\n" );
			}else{
				ins_model_cmd( p_model );
			}
			break;
		case 'q':
			return;
		case ' ':
		case '\t':
		case '\n':
			break;
		default:
			printf( "cmd error:%d\n", cmd );
		}
	}
}

