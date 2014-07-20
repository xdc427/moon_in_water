#include"test/shadow_struct_test.h"
void version_test_code_init()
{
	mem_block cur_block;
	version_info cur_version;
	double_list version_head;
	shadow_type new_shadow;
	id_pair pair, tmp;
	int i;

	cur_version = NULL;
  new_shadow = hash_search( shadow_head, "test_code", sizeof( shadow_type_s ) );
  new_shadow->struct_types = shadow_struct_test_code_init( );
	pthread_mutex_init( &new_shadow->entity_mutex, NULL );
	version_head = ( double_list ) calloc( 1, sizeof( double_list_s ) + sizeof( version_info_s ) );
	if( version_head == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		goto error;
	}
	cur_version = ( version_info )( version_head + 1 );
	cur_version->ref_num = 1;
	cur_version->block_num = 3;
	cur_version->order_id = ( addr_pair )calloc( 1, sizeof( id_pair_s ) * 3 );
	if( cur_version->order_id == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		goto error;
	}
	pair = cur_version->order_id;
	for( i = 0, tmp = pair; i < 3; i++, tmp++ ){
		tmp->addr = malloc( sizeof( mem_block_s ) );
		if( tmp->addr == NULL ){
			MOON_PRINT_MAN( ERROR, "malloc error!" );
			goto error;
		}
	}
	pair->id = 1;
	cur_block = pair->addr;
	cur_block->id = 1;
	cur_block->version = 0;
	cur_block->virtual_addr = get_test_code_head_addr();
	cur_block->is_del = 0;
	cur_block->is_root = 1;
	cur_block->data = ( char * )get_test_code_head_addr();
	cur_block->type_num = get_test_code_head_len();
	cur_block->data_len = get_test_code_head_size();
	cur_block->type_id = find_type( new_shadow->struct_types, "struct list_s *" );
	if( cur_block->type_id < 0 ){
		MOON_PRINT_MAN( ERROR, "can,t find type!" );
		goto error;
	}
	pair++;
	pair->id = 2;
	cur_block = pair->addr;
	cur_block->id = 2;
	cur_block->version = 0;
	cur_block->virtual_addr = get_test_code_data_addr();
	cur_block->is_del = 0;
	cur_block->is_root = 1;
	cur_block->data = ( char * )get_test_code_data_addr();
	cur_block->type_num = get_test_code_data_len();
	cur_block->data_len = get_test_code_data_size();
	cur_block->type_id = find_type( new_shadow->struct_types, "int" );
	if( cur_block->type_id < 0 ){
		MOON_PRINT_MAN( ERROR, "can,t find type!" );
		goto error;
	}
	pair++;
	pair->id = 3;
	cur_block = pair->addr;
	cur_block->id = 3;
	cur_block->version = 0;
	cur_block->virtual_addr = get_test_code_data_point_addr();
	cur_block->is_del = 0;
	cur_block->is_root = 1;
	cur_block->data = ( char * )get_test_code_data_point_addr();
	cur_block->type_num = get_test_code_data_point_len();
	cur_block->data_len = get_test_code_data_point_size();
	cur_block->type_id = find_type( new_shadow->struct_types, "int *" );
	if( cur_block->type_id < 0 ){
		MOON_PRINT_MAN( ERROR, "can,t find type!" );
		goto error;
	}
	pair++;
	cur_version->order_addr = sort_addr_id( cur_version->order_id, 3 );
	if( cur_version->order_addr == NULL ){
		MOON_PRINT_MAN( ERROR, "create sorted addr array error!" );
		goto error;
	}
	new_shadow->init_version = version_head;
	return;
error:
	version_free( cur_version, DEL_ORDER_IDS );
}
void version_init()
{
	shadow_env_init();
	version_test_code_init();
}
