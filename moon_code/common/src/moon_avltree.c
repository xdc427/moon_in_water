#include"moon_common.h"
#include<stdlib.h>
#include<stdio.h>

static int addr_pair_cmp_func( void * p0, common_user_data_u user_data )
{
	uint64_t u64_0, u64_1;

	u64_0 = ( ( addr_pair )p0 )->virtual_addr;
	u64_1 = user_data.ull_num;
	if( u64_0 > u64_1 ){
		return 1;
	}else if( u64_0 < u64_1 ){
		return -1;
	}else{
		return 0;
	}
}

int avl_traver_preorder( avl_tree avl, traver_func func, common_user_data_u user_data )
{
	int ret = 0;

	if( avl == NULL
		|| ( ret = func( avl + 1, user_data ) ) != 0
		|| ( ret = avl_traver_preorder( avl->left, func, user_data ) ) != 0
		|| ( ret = avl_traver_preorder( avl->right, func, user_data ) ) != 0 ){
		return ret;
	}
	return 0;
}

int avl_traver_midorder( avl_tree avl, traver_func func, common_user_data_u user_data )
{
	int ret = 0;

	if( avl == NULL
		|| ( ret = avl_traver_midorder( avl->left, func, user_data ) ) != 0
		|| ( ret = func( avl + 1, user_data ) ) != 0
		|| ( ret = avl_traver_midorder( avl->right, func, user_data ) ) != 0 ){
		return ret;
	}
	return 0;
}

int avl_traver_lastorder( avl_tree avl, traver_func func, common_user_data_u user_data )
{
	int ret = 0;

	if( avl == NULL
		|| ( ret = avl_traver_lastorder( avl->left, func, user_data ) ) != 0
		|| ( ret = avl_traver_lastorder( avl->right, func, user_data ) ) != 0
		|| ( ret = func( avl + 1, user_data ) ) != 0 ){
		return ret;
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

void * avl_leftest_node( avl_tree avl )
{
	if( avl != NULL ){
		for( ; avl->left != NULL; avl = avl->left ){
			;
		}
		return avl + 1;
	}
	return NULL;
}

void * avl_rightest_node( avl_tree avl )
{
	if( avl != NULL ){
		for( ; avl->right != NULL; avl = avl->right ){
			;
		}
		return avl + 1;
	}
	return NULL;
}

void * avl_search2( avl_tree avl, cmp_func cmp, common_user_data_u user_data )
{
	int ret;
	void * prev, * cur;

	prev = NULL;
	while( avl != NULL ){
		cur = avl + 1;
		ret = cmp( cur, user_data );
		if( ret > 0 ){
			avl = avl->left;
		}else if( ret < 0 ){
			prev = cur;
			avl = avl->right;
		}else{
			prev = cur;
			break;
		}
	}
	return prev;
}

addr_pair avl_search( avl_tree avl, unsigned long long addr )
{
	common_user_data_u user_data;

	user_data.ull_num = addr;
	return avl_search2( avl, addr_pair_cmp_func, user_data );
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

void * avl_add2( avl_tree * pavl, void * p_data, cmp_func cmp, common_user_data_u user_data )
{
	avl_tree new_node, * tmp_pavl, parent_avl, tmp_avl;
	void * p_cmp_data;
	int balance, ret;

	if( pavl == NULL || p_data == NULL ){
		MOON_PRINT_MAN( ERROR, "input paraments error!" );
		return NULL;
	}
	new_node = ( avl_tree )p_data - 1;
	tmp_pavl = pavl;
	parent_avl = NULL;
	while( *tmp_pavl != NULL ){
		parent_avl = *tmp_pavl;
		p_cmp_data = *tmp_pavl + 1;
		ret = cmp( p_cmp_data, user_data );
		if( ret > 0 ){
			tmp_pavl = &( *tmp_pavl )->left;
		}else if( ret < 0  ){
			tmp_pavl = &( *tmp_pavl )->right;
		}else{
			return p_cmp_data;
		}
	}
	*tmp_pavl = new_node;
	new_node->parent = parent_avl;
	tmp_avl = new_node;
	while( tmp_avl-> parent != NULL ){
		parent_avl = tmp_avl->parent;
		balance = 1;
		if( parent_avl->left == tmp_avl ){
			balance = -1;
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
		break;
	}
	return p_data;
}

addr_pair avl_add( avl_tree * pavl, unsigned long long addr )
{
	avl_tree new_node;
	addr_pair pair;
	void * p_data;
	common_user_data_u user_data;

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
	user_data.ull_num = addr;
	p_data = avl_add2( pavl, pair, addr_pair_cmp_func, user_data );
	if( p_data != pair ){
		free( new_node );
	}
	return pair;
}

static inline void replace_avl( avl_tree new, avl_tree replaced, avl_tree * pavl )
{
	new->balance = replaced->balance;
	SET_LR( new, left, replaced->left );
	SET_LR( new, right, replaced->right );
	SET_PARENT( new, replaced, pavl );
}

void * avl_del2( avl_tree * pavl, void * p_data )
{
	int balance;
	avl_tree tmp_avl, del_avl, parent_avl, child_avl;
	avl_tree mark_avl1, mark_avl2;

	if( pavl == NULL || p_data == NULL ){
		return NULL;
	}
	mark_avl1 = mark_avl2 = NULL;
	tmp_avl = ( avl_tree )p_data - 1;
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
	mark_avl1 = del_avl;
	del_avl = tmp_avl;
	if( child_avl != NULL ){
		mark_avl2 = del_avl;
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
		if( mark_avl2 != NULL ){
			replace_avl( del_avl, mark_avl2, pavl );
			del_avl = mark_avl2;
		}
		if( mark_avl1 != NULL ){
			replace_avl( del_avl, mark_avl1, pavl );
			del_avl = mark_avl1;
		}
	}else{
		*pavl = NULL;
	}
	del_avl->balance = 0;
	del_avl->left = del_avl->right = del_avl->parent = NULL;
	return del_avl + 1;
}

addr_pair_s avl_del( avl_tree * pavl, unsigned long long addr )
{
	addr_pair_s del_pair;
	addr_pair p_pair;

	memset( &del_pair, 0, sizeof( del_pair ) );
	if( pavl == NULL ){
		return del_pair;
	}
	p_pair = avl_search( *pavl, addr );
	if( p_pair != NULL 
		&& p_pair->virtual_addr == addr ){
		del_pair = *p_pair;
		avl_del2( pavl, p_pair );
		free( ( avl_tree )p_pair - 1 );
	}
	return del_pair;
}

void avl_replace2( avl_tree * pavl, void * p_new, void * p_replaced )
{
	avl_tree new, replaced;
	
	if( pavl != NULL && p_new != NULL && p_replaced != NULL ){
		new = ( avl_tree )p_new - 1;
		replaced = ( avl_tree )p_replaced - 1;
		replace_avl( new, replaced, pavl );
		replaced->balance = 0;
		replaced->left = replaced->right = replaced->parent = NULL;
	}
}

