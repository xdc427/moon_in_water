#include<stdio.h>
#include<regex.h>
#include"shadow_base.h"
#include"shadow_struct.h"

int find_type( struct_all head, char * type )
{
	regex_t regex;
	int err;
	struct_one * one_arr;
	int i;

	if( head == NULL || type == NULL ){
		return -1;
	}
	one_arr = ( struct_one * )( head + 1 );

	for( i = 0; i < head->num; i++){
		err = regcomp( &regex, one_arr[ i ]->name, REG_EXTENDED );
		if( err != 0){
			//need print err info
			return -1;
		}
		err = regexec( &regex, type, 0, NULL, 0 );
		regfree( &regex );
		if( err == 0 ){
			return i;
		}else if( err == REG_NOMATCH ){
			continue;
		}else{
			//need print err info
			return -1;
		}
	}
}

void complete_var_type( struct_all head )
{
	struct_one * one_arr;
	struct_one tmp_one;
	var tmp_var;
	int i, j, id;

	one_arr = ( struct_one * )( head + 1 );

	for( i = 0; i < head->num; i++ ){
		tmp_one = one_arr[ i ];
		for( j =0; j < tmp_one->num; j++ ){
			tmp_var = ( var )( tmp_one + 1 );
			id = find_type( head, tmp_var->type );
			if( id < 0 ){
				//need print err info
				return;
			}
			tmp_var->type_num = id;
		}
	}
}

