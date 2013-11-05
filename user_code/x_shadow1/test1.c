#include<stdio.h>
#include"moonpp.h"

SHADOW_STRUCT struct list_shadow1_s{
	int id;
	struct list_shadow1_s * next;
};
typedef struct list_shadow1_s list_shadow1_s;
typedef struct list_shadow1_s * list_shadow1;

SHADOW_VAR struct list_shadow1_s * head = NULL ; //shadow变量目前不能用别名声明

void add( int id )
{
	SHADOW_POINT struct list_shadow1_s ** tmp;
	SHADOW_POINT struct list_shadow1_s * new;
	tmp = SHADOW_ADDRESS( &head );
	for( ; SHADOW_POINT_CMP( tmp, !=, NULL )
			; tmp = SHADOW_ADDRESS( &( *tmp )->next ) ){
		;
	}
	new = SHADOW_NEW( struct list_shadow1_s, 1 );
	SHADOW_SET( &new->id, id );
	SHADOW_SET( tmp, new );
}

void main()
{
	SHADOW_POINT struct list_shadow1_s * tmp;
	add( 2 );
	add( 3 );
	add( 4 );
	for( tmp = SHADOW_GET( &head ); SHADOW_POINT_CMP( &tmp, !=, NULL ) 
			; tmp = SHADOW_GET( &tmp->next ) ){
		printf( "%d->", SHADOW_GET( &tmp->id ) );
	}
	printf( "\n" );
}
