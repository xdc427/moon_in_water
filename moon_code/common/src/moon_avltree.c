#include"moon_common.h"
#include<stdlib.h>
#include<stdio.h>


int avl_traver_first( avl_tree avl, int ( *func )( addr_pair, void * ), void * para )
{
	addr_pair pair;

	if( avl == NULL ){
		return 0;
	}
	if( avl_traver_first( avl->left, func, para ) < 0 ){
		return -1;
	}
	pair = ( addr_pair )( avl + 1 ); 
	if( func( pair, para ) < 0 ){
		return -1;
	}
	if( avl_traver_first( avl->right, func, para ) < 0 ){
		return -1;
	}
	return 0;
}

void avl_print( avl_tree avl )
{
	addr_pair pair;
	if( avl == NULL ){
		return;
	}
	avl_print( avl->left );
	pair = ( addr_pair )( avl + 1 );
	printf( "[ 0x%llx:%d ]", pair->virtual_addr, avl->balance );
	avl_print( avl->right );
	if( avl->parent == NULL ){
		printf( "\n" );
	}
}

void avl_free( avl_tree * pavl )
{
	if( *pavl != NULL ){
		avl_free( &( *pavl )->left );
		avl_free( &( *pavl )->right );
		free( *pavl );
		*pavl = NULL;
	}
}

addr_pair avl_leftest_node( avl_tree avl )
{
	if( avl == NULL ){
		return NULL;
	}
	for( ; avl->left != NULL; avl = avl->left ){
		;
	}
	return ( addr_pair )( avl + 1 );
}

addr_pair avl_search( avl_tree avl, unsigned long long addr )
{
	avl_tree tmp_avl;
	addr_pair pair, last;

	last = NULL;
	tmp_avl = avl;
	while( tmp_avl != NULL ){
		pair = ( addr_pair )( tmp_avl + 1 );
		if( pair->virtual_addr > addr ){
			tmp_avl = tmp_avl->left;
		}else if( pair->virtual_addr == addr ){
			return pair;
		}else{
			last = pair;
			tmp_avl = tmp_avl->right;
		}
	}
	return last;
}

#define SET_LR( node, side, child_node) do{\
	( node )->side = ( child_node ); \
	if( ( node )->side != NULL ){ \
		( node )->side->parent = ( node ); \
	} \
}while( 0 )

#define SET_PARENT( node, other_node, pavl ) do{\
	( node )->parent = ( other_node )->parent; \
	if( ( node )->parent == NULL ){ \
		*( pavl ) = ( node ); \
	}else if( ( node )->parent->left == ( other_node ) ){ \
		( node )->parent->left = ( node ); \
	}else{ \
		( node )->parent->right = ( node ); \
	} \
}while( 0 )

static inline avl_tree avl_balance( avl_tree parent_avl, avl_tree * pavl )
{
	avl_tree tmp_avl, axis_avl;

	if( parent_avl->balance == 2 ){//设置left或right后立马设置其parent
		tmp_avl = parent_avl->right;
		if( tmp_avl->balance >= 0 ){
			axis_avl = tmp_avl;
			SET_LR( parent_avl, right, axis_avl->left );
			SET_PARENT( axis_avl, parent_avl, pavl );
			SET_LR( axis_avl, left, parent_avl );

			if( axis_avl->balance == 0 ){
				parent_avl->balance = 1;
				axis_avl->balance = -1;
				return NULL;
			}else{
				axis_avl->balance = 0;
				parent_avl->balance = 0;
			}
		}else{
			axis_avl = tmp_avl->left;

			SET_LR( parent_avl, right, axis_avl->left );
			SET_LR( tmp_avl, left, axis_avl->right );
			SET_PARENT( axis_avl, parent_avl, pavl );
			SET_LR( axis_avl, left, parent_avl );
			SET_LR( axis_avl, right, tmp_avl );

			parent_avl->balance = 0;
			tmp_avl->balance = 0;
			if( axis_avl->balance == 1 ){
				parent_avl->balance = -1;
			}else if( axis_avl->balance == -1 ){
				tmp_avl->balance = 1;
			}
			axis_avl->balance = 0;
		}
	}else{
		tmp_avl = parent_avl->left;
		if( tmp_avl->balance <= 0 ){
			axis_avl = tmp_avl;
			SET_LR( parent_avl, left, axis_avl->right );
			SET_PARENT( axis_avl, parent_avl, pavl );
			SET_LR( axis_avl, right, parent_avl );

			if( axis_avl->balance == 0 ){
				parent_avl->balance = -1;
				axis_avl->balance = 1;
				return NULL;
			}else{
				axis_avl->balance = 0;
				parent_avl->balance = 0;
			}
		}else{
			tmp_avl = parent_avl->left;
			axis_avl = tmp_avl->right;

			SET_LR( parent_avl, left, axis_avl->right );
			SET_LR( tmp_avl, right, axis_avl->left );
			SET_PARENT( axis_avl, parent_avl, pavl );
			SET_LR( axis_avl, left, tmp_avl );
			SET_LR( axis_avl, right, parent_avl );

			parent_avl->balance = 0;
			tmp_avl->balance = 0;
			if( axis_avl->balance == 1 ){
				tmp_avl->balance = -1;
			}else if( axis_avl->balance == -1 ){
				parent_avl->balance = 1;
			}
			axis_avl->balance = 0;
		}
	}

	return axis_avl;
}

addr_pair avl_add( avl_tree * pavl, unsigned long long addr )
{
	avl_tree new_node, * tmp_pavl, parent_avl, tmp_avl;
	addr_pair pair;
	int balance;

	if( pavl == NULL ){
		MOON_PRINT_MAN( ERROR, "input paraments error!" );
		return NULL;
	}
	new_node = calloc( 1, sizeof( avl_tree_s ) + sizeof( addr_pair_s ) );
	if( new_node == NULL ){
		MOON_PRINT_MAN( ERROR, "malloc error!" );
		return NULL;
	}
	pair = ( addr_pair )( new_node + 1 );
	pair->virtual_addr = addr;

	tmp_pavl = pavl;
	parent_avl = NULL;
	while( *tmp_pavl != NULL ){
		parent_avl = *tmp_pavl;
		pair = ( addr_pair )( *tmp_pavl + 1 );
		if( pair->virtual_addr > addr ){
			tmp_pavl = &( *tmp_pavl )->left;
		}else if( pair->virtual_addr == addr ){
			free( new_node );
			return pair;
		}else{
			tmp_pavl = &( *tmp_pavl )->right;
		}
	}

	*tmp_pavl = new_node;
	new_node->parent = parent_avl;
	tmp_avl = new_node;
	while( tmp_avl-> parent != NULL ){
		parent_avl = tmp_avl->parent;
		if( parent_avl->left == tmp_avl ){
			balance = -1;
		}else{
			balance = 1;
		}
		parent_avl->balance += balance;
		switch( parent_avl->balance ){
		case 0:
			break;
		case 1:
		case -1:
			tmp_avl = parent_avl;
			continue;
		case 2:
		case -2:
			avl_balance( parent_avl, pavl );
			break;
		default:
			MOON_PRINT_MAN( ERROR, "dont balance" );
		}
		return ( addr_pair )( new_node + 1 );
	}
	return ( addr_pair )( new_node + 1 );
}

addr_pair_s avl_del( avl_tree * pavl, unsigned long long addr )
{
	addr_pair pair, pair2;
	int balance;
	avl_tree tmp_avl, del_avl, parent_avl, child_avl;
	addr_pair_s del_pair;

	memset( &del_pair, 0, sizeof( del_pair ) );
	if( pavl == NULL ){
		return del_pair;
	}
	tmp_avl = *pavl;
	while( tmp_avl != NULL ){
		pair = ( addr_pair )( tmp_avl + 1 );
		if( pair->virtual_addr > addr ){
			tmp_avl = tmp_avl->left;
		}else if( pair->virtual_addr == addr ){
			del_pair = *pair;
			break;
		}else{
			tmp_avl = tmp_avl->right;
		}
	}
	if( tmp_avl == NULL ){
		return del_pair;
	}

	del_avl = tmp_avl;
	if( tmp_avl->left == NULL && tmp_avl->right == NULL ){//为叶子节点
		goto start_back;//以此节点开始上溯
	}
	if( tmp_avl->balance <= 0 ){
		tmp_avl = tmp_avl->left;
		for( ; tmp_avl->right != NULL; tmp_avl = tmp_avl->right ){
			;
		}
		child_avl = tmp_avl->left;
	}else{
		tmp_avl = tmp_avl->right;
		for( ; tmp_avl->left != NULL; tmp_avl = tmp_avl->left ){
			;
		}
		child_avl = tmp_avl->right;
	}
	pair =( addr_pair )( tmp_avl + 1 );
	pair2 = ( addr_pair )( del_avl + 1 );
	*pair2 = *pair;
	del_avl = tmp_avl;
	if( child_avl != NULL ){
		pair2 = ( addr_pair )( child_avl + 1 );
		*pair = *pair2;
		del_avl = child_avl;
	}

start_back:
	tmp_avl = del_avl;
	while( tmp_avl->parent != NULL ){
		parent_avl = tmp_avl->parent;
		if( parent_avl->left == tmp_avl ){
			balance = 1;
		}else{
			balance = -1;
		}
		parent_avl->balance += balance;
		switch( parent_avl->balance ){
		case 1:
		case -1:
			goto end;
		case 0:
			tmp_avl = tmp_avl->parent;
			break;
		case 2:
		case -2:
			tmp_avl = avl_balance( parent_avl, pavl );
			if( tmp_avl == NULL ){
				goto end;
			}
			break;
		}
	}
end:
	parent_avl = del_avl->parent;
	if( parent_avl != NULL ){
		if( parent_avl->left == del_avl ){
			parent_avl->left = NULL;
		}else{
			parent_avl->right = NULL;
		}
	}else{
		*pavl = NULL;
	}
	free( del_avl );

	return del_pair;
}

#ifdef LEVEL_TEST

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
				printf( "search ok:%lld\n", ret->virtual_addr );
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

#endif

