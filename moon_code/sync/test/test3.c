#include<stdio.h>

struct test{
	char a;
	char b;
	long c;
};

void main()
{
	printf( "%d:%d:%d:%d\n", sizeof( struct test ), sizeof( ( ( struct test * )0 )->a )
			, &( ( struct test * )0 )->c, &( ( struct test * )0 )->b );
}
