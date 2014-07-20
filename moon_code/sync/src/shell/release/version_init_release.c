#include"release/shadow_struct_release.h"
void version_x_shadow1_init()
{
	mem_block cur_block;
	version_info cur_version;
	double_list version_head;
	shadow_type new_shadow;
	id_pair pair, tmp;
	int i;

	cur_version = NULL;
  new_shadow = hash_search( shadow_head, "x_shadow1", sizeof( shadow_type_s ) );
  new_shadow->struct_types = shadow_struct_x_shadow1_init( );
	pthread_mutex_init( &new_shadow->entity_mutex, NULL );
	version_head = ( double_list ) calloc( 1, sizeof( double_list_s ) + sizeof( version_info_s ) );
	if( version_head == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		goto error;
	}
	cur_version = ( version_info )( version_head + 1 );
	cur_version->ref_num = 1;
	cur_version->block_num = 1;
	cur_version->order_id = ( addr_pair )calloc( 1, sizeof( id_pair_s ) * 1 );
	if( cur_version->order_id == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		goto error;
	}
	pair = cur_version->order_id;
	for( i = 0, tmp = pair; i < 1; i++, tmp++ ){
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
	cur_block->virtual_addr = get_x_shadow1_head_addr();
	cur_block->is_del = 0;
	cur_block->is_root = 1;
	cur_block->data = ( char * )get_x_shadow1_head_addr();
	cur_block->type_num = get_x_shadow1_head_len();
	cur_block->data_len = get_x_shadow1_head_size();
	cur_block->type_id = find_type( new_shadow->struct_types, "struct list_shadow1_s *" );
	if( cur_block->type_id < 0 ){
		MOON_PRINT_MAN( ERROR, "can,t find type!" );
		goto error;
	}
	pair++;
	cur_version->order_addr = sort_addr_id( cur_version->order_id, 1 );
	if( cur_version->order_addr == NULL ){
		MOON_PRINT_MAN( ERROR, "create sorted addr array error!" );
		goto error;
	}
	new_shadow->init_version = version_head;
	return;
error:
	version_free( cur_version, DEL_ORDER_IDS );
}
void version_x_shadow2_init()
{
	mem_block cur_block;
	version_info cur_version;
	double_list version_head;
	shadow_type new_shadow;
	id_pair pair, tmp;
	int i;

	cur_version = NULL;
  new_shadow = hash_search( shadow_head, "x_shadow2", sizeof( shadow_type_s ) );
  new_shadow->struct_types = shadow_struct_x_shadow2_init( );
	pthread_mutex_init( &new_shadow->entity_mutex, NULL );
	version_head = ( double_list ) calloc( 1, sizeof( double_list_s ) + sizeof( version_info_s ) );
	if( version_head == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		goto error;
	}
	cur_version = ( version_info )( version_head + 1 );
	cur_version->ref_num = 1;
	cur_version->block_num = 1;
	cur_version->order_id = ( addr_pair )calloc( 1, sizeof( id_pair_s ) * 1 );
	if( cur_version->order_id == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		goto error;
	}
	pair = cur_version->order_id;
	for( i = 0, tmp = pair; i < 1; i++, tmp++ ){
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
	cur_block->virtual_addr = get_x_shadow2_head_addr();
	cur_block->is_del = 0;
	cur_block->is_root = 1;
	cur_block->data = ( char * )get_x_shadow2_head_addr();
	cur_block->type_num = get_x_shadow2_head_len();
	cur_block->data_len = get_x_shadow2_head_size();
	cur_block->type_id = find_type( new_shadow->struct_types, "struct list_shadow2_s *" );
	if( cur_block->type_id < 0 ){
		MOON_PRINT_MAN( ERROR, "can,t find type!" );
		goto error;
	}
	pair++;
	cur_version->order_addr = sort_addr_id( cur_version->order_id, 1 );
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
	version_x_shadow1_init();
	version_x_shadow2_init();
}
