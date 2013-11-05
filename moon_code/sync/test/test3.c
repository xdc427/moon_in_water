#include<stdio.h>

struct test{
	char a;
	char b;
	long c;
};

void main()
{
	int a;
	char * p_a;

	printf( "%d:%d:%d:%d\n", sizeof( struct test ), sizeof( ( ( struct test * )0 )->a )
			, &( ( struct test * )0 )->c, &( ( struct test * )0 )->b );

	p_a = ( char * )&a;
	*( int * )p_a = 1;
	p_a = ( int * )p_a + 1;
	printf( "%d:%p:%p", 1, &a, p_a );
}
