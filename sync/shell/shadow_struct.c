#include<stdio.h>
#include"shadow_base.h"
#include"shadow_struct.h"

static char buffer[ sizeof( struct_all_s ) + sizeof( struct_one_s ) * 2 + sizeof( var_s ) * 6 ];

void shadow_struct_init()
{
	struct_all struct_head;
	struct_one tmp_one;
	var tmp_var;
	int i;

	struct_head = ( struct_all )buffer;
	struct_head->num = 2;
	tmp_var = ( var )( struct_head + 1 ) - 1;
	tmp_one = ( struct_one )( tmp_var + 1 );
	snprintf( tmp_one->name , sizeof( tmp_one->name ), "list_s" );
	tmp_one->size = sizeof( struct list_s );
	tmp_one->num = 2;
	tmp_var = ( var )( tmp_one + 1 ) - 1;
	tmp_var++;
	snprintf( tmp_var->name, sizeof( tmp_var->name ), "id" );
	snprintf( tmp_var->type, sizeof( tmp_var->type ), "int" );
	tmp_var->size = sizeof( ( ( struct list_s * )0 )->id );
	tmp_var->size = &( ( ( struct list_s * )0 )->id );
	tmp_var->array_d = 0;
	tmp_var++;
	snprintf( tmp_var->name, sizeof( tmp_var->name ), "next" );
	snprintf( tmp_var->type, sizeof( tmp_var->type ), "struct list_s *" );
	tmp_var->size = sizeof( ( ( struct list_s * )0 )->next );
	tmp_var->size = &( ( ( struct list_s * )0 )->next );
	tmp_var->array_d = 0;
	tmp_one = ( struct_one )( tmp_var + 1 );
	snprintf( tmp_one->name , sizeof( tmp_one->name ), "list" );
	tmp_one->size = sizeof( struct list );
	tmp_one->num = 4;
	tmp_var = ( var )( tmp_one + 1 ) - 1;
	tmp_var++;
	snprintf( tmp_var->name, sizeof( tmp_var->name ), "id" );
	snprintf( tmp_var->type, sizeof( tmp_var->type ), "long int** * *" );
	tmp_var->size = sizeof( ( ( struct list * )0 )->id );
	tmp_var->size = &( ( ( struct list * )0 )->id );
	tmp_var->array_d = 2;
	tmp_var++;
	snprintf( tmp_var->name, sizeof( tmp_var->name ), "num" );
	snprintf( tmp_var->type, sizeof( tmp_var->type ), "int" );
	tmp_var->size = sizeof( ( ( struct list * )0 )->num );
	tmp_var->size = &( ( ( struct list * )0 )->num );
	tmp_var->array_d = 0;
	tmp_var++;
	snprintf( tmp_var->name, sizeof( tmp_var->name ), "a" );
	snprintf( tmp_var->type, sizeof( tmp_var->type ), "unsigned int" );
	tmp_var->size = sizeof( ( ( struct list * )0 )->a );
	tmp_var->size = &( ( ( struct list * )0 )->a );
	tmp_var->array_d = 0;
	tmp_var++;
	snprintf( tmp_var->name, sizeof( tmp_var->name ), "student" );
	snprintf( tmp_var->type, sizeof( tmp_var->type ), "struct stu **	*" );
	tmp_var->size = sizeof( ( ( struct list * )0 )->student );
	tmp_var->size = &( ( ( struct list * )0 )->student );
	tmp_var->array_d = 1;
	return;
}
